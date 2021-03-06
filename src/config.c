// config.c support parsing config file and apply config to settings
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "wheatserver.h"

static struct enumIdName Verbose[] = {
    {WHEAT_DEBUG, "DEBUG"}, {WHEAT_VERBOSE, "VERBOSE"},
    {WHEAT_NOTICE, "NOTICE"}, {WHEAT_WARNING, "WARNING"},
    {-1, NULL}
};

static struct enumIdName Workers[] = {
    {0, "SyncWorker"}, {1, "AsyncWorker"}
};

// configTable is immutable after worker setuped in *Worker Process*, and it
// only used for master process and general settings. Applicant, protocol and
// worker module can't add own config to `configTable`.
//
// Attention: If modify configTable, three places should be attention to.
// 1. initGlobalServerConfig() in wheatserver.c
// 2. wheatserver.conf
// 3. fillServerConfig() (see below)
struct configuration configTable[] = {
    //  name   |    args   |      validator      |      default       |
    //  helper |    flags

    // Master Configuration
    {"protocol",          2, stringValidator,      {.ptr=WHEAT_PROTOCOL_DEFAULT},
        (void *)WHEAT_NOTFREE,  STRING_FORMAT},
    {"bind-addr",         2, stringValidator,      {.ptr=WHEAT_DEFAULT_ADDR},
        (void *)WHEAT_NOTFREE,  STRING_FORMAT},
    {"port",              2, unsignedIntValidator, {.val=WHEAT_SERVERPORT},
        NULL,                   INT_FORMAT},
    {"worker-number",     2, unsignedIntValidator, {.val=2},
        (void *)1024,           INT_FORMAT},
    {"worker-type",       2, enumValidator,        {.enum_ptr=&Workers[0]},
        &Workers[0],            ENUM_FORMAT},
    {"logfile",           2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"logfile-level",     2, enumValidator,        {.enum_ptr=&Verbose[2]},
        &Verbose[0],            ENUM_FORMAT},
    {"daemon",            2, boolValidator,        {.val=0},
        NULL,                   BOOL_FORMAT},
    {"pidfile",           2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"max-buffer-size",   2, unsignedIntValidator, {.val=WHEAT_MAX_BUFFER_SIZE},
        (void *)WHEAT_BUFLIMIT, INT_FORMAT},
    {"stat-bind-addr",    2, stringValidator,      {.ptr=WHEAT_STATS_ADDR},
        (void *)WHEAT_NOTFREE, STRING_FORMAT},
    {"stat-port",         2, unsignedIntValidator, {.val=WHEAT_STATS_PORT},
        NULL,                   INT_FORMAT},
    {"stat-refresh-time", 2, unsignedIntValidator, {.val=WHEAT_STAT_REFRESH},
        NULL,                   INT_FORMAT},
    {"stat-file",         2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"timeout-seconds",   2, unsignedIntValidator, {.val=WHEATSERVER_TIMEOUT},
        (void *)300,            INT_FORMAT},
    {"mbuf-size",         2, unsignedIntValidator, {.val=WHEAT_MBUF_SIZE},
        (void *)WHEAT_BUFLIMIT, INT_FORMAT},
};

// fillServerConfig is used to fill configTable values to global variable
// `Server`.
// But implement is ugly and difficult to modify by newer. It's need refactor
// priorly.
// TODO: change fillServerConfig implement or remove it.
void fillServerConfig()
{
    struct configuration *conf = &configTable[1];

    Server.bind_addr = conf->target.ptr;
    conf++;
    Server.port = conf->target.val;
    conf++;
    Server.worker_number = conf->target.val;
    conf++;
    Server.worker_type = conf->target.enum_ptr->name;
    conf++;
    Server.logfile = conf->target.ptr;
    conf++;
    Server.verbose = conf->target.enum_ptr->id;
    conf++;
    Server.daemon = conf->target.val;
    conf++;
    Server.pidfile = conf->target.ptr;
    conf++;
    Server.max_buffer_size = conf->target.val;
    conf++;
    Server.stat_addr = conf->target.ptr;
    conf++;
    Server.stat_port = conf->target.val;
    conf++;
    Server.stat_refresh_seconds = conf->target.val;
    conf++;
    Server.stat_file = conf->target.ptr;
    conf++;
    Server.worker_timeout = conf->target.val;
    conf++;
    Server.mbuf_size = conf->target.val;
}

/* ========== Configuration Validator/Print Area ========== */

// There is a trick to handle. When STRING_FORMAT config using wstr type to
// store, but compiled init value is constant string. We should check it
// constant string or wstr type by `helper` filed.
int stringValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);

    if (conf->target.ptr && !conf->helper) {
        wfree(conf->target.ptr);
        conf->helper = NULL;
    }
    if (!strncasecmp(val, WHEAT_STR_NULL, sizeof(WHEAT_STR_NULL)))
        conf->target.ptr = NULL;
    else
        conf->target.ptr = strdup(val);
    return VALIDATE_OK;
}

// Do nothing special but free list
int listValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    if (conf->target.ptr) {
        freeList(conf->target.ptr);
    }

    conf->target.ptr = (struct list *)val;
    return VALIDATE_OK;
}

// Check config val is a unsigned integer and use `helper` field to limit its
// upper bound.
int unsignedIntValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    int i, digit, value;
    intptr_t max_limit = 0;

    if (conf->helper)
        max_limit = (intptr_t)conf->helper;

    for (i = 0; val[i] != '\0'; i++) {
        digit = val[i] - 48;
        if (digit < 0 || digit > 9)
            return VALIDATE_WRONG;
        // Avoid too long int value
        if (i >= 10)
            return VALIDATE_WRONG;
    }

    value = atoi(val);
    if (max_limit != 0 && value > max_limit)
        return VALIDATE_WRONG;
    conf->target.val = value;
    return VALIDATE_OK;
}

int enumValidator(struct configuration *conf, const char *key, const char *val)
{
    ASSERT(val);
    ASSERT(conf->helper);

    struct enumIdName *sentinel = (struct enumIdName *)conf->helper;
    while (sentinel->name) {
        if (strncasecmp(val, sentinel->name, strlen(sentinel->name)) == 0) {
            conf->target.enum_ptr = sentinel;
            return VALIDATE_OK;
        }
        else if (sentinel == NULL)
            break ;
        sentinel++;
    }

    return VALIDATE_WRONG;
}

int boolValidator(struct configuration *conf, const char *key, const char *val)
{
    size_t len = strlen(val) + 1;
    if (memcmp("on", val, len) == 0) {
        conf->target.val = 1;
    } else if (memcmp("off", val, len) == 0) {
        conf->target.val = 0;
    } else
        return VALIDATE_WRONG;
    return VALIDATE_OK;
}

// It used by WheatServer frameworker but module programmers
static void extraValidator()
{
    ASSERT(Server.stat_refresh_seconds < Server.worker_timeout);
    ASSERT(Server.port && Server.stat_port);
}

/* ================ Handle Configuration ================ */

void initServerConfs(struct list *confs)
{
    struct configuration *conf;
    int len, i;

    len = sizeof(configTable) / sizeof(struct configuration);
    i = 0;
    while (i != len) {
        conf = &configTable[i];
        appendToListTail(confs, conf);
        i++;
    }
}

// Consider to return actual value from union structure and reduce module
// programmer's job.
//
// TODO: Modify getConfiguration return type
struct configuration *getConfiguration(const char *name)
{
    struct listNode *node;
    struct listIterator *iter;
    struct configuration *conf;

    iter = listGetIterator(Server.confs, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        conf = listNodeValue(node);
        if (!strncasecmp(name, conf->name, strlen(name))) {
            freeListIterator(iter);
            return conf;
        }
    }
    freeListIterator(iter);

    return NULL;
}

// Parse config string to list structure
static struct list *handleList(wstr *lines, int *i, int count)
{
    int pos = *i + 1;
    wstr line = NULL;
    struct list *l = createList();
    listSetFree(l, (void(*)(void *))wstrFree);
    while (pos < count) {
        line = wstrStrip(lines[pos], "\t\n\r ");
        int len = 0;
        int line_valid = 0;
        while (len < wstrlen(line)) {
            char ch = line[len];
            switch(ch) {
                case ' ':
                    len++;
                    break;
                case '-':
                    if (!line_valid)
                        line_valid = 1;
                    else
                        goto handle_error;
                    len++;
                    break;
                default:
                    if (!line_valid)
                        goto handle_error;
                    appendToListTail(l, wstrNew(&line[len]));
                    len = wstrlen(line);
                    break;
            }
        }
        pos++;
    }

handle_error:
    pos--;
    *i = pos;
    return l;
}

// `config`: Multi lines from config file and command line. They are seperate
// by "\n".
//
// Seperate lines and iterate each line to check somethings listed below:
// 1. correspond config field
// 2. correct config value type and constraints
// 3. Validate by validator functions
void applyConfig(wstr config)
{
    int count, args;
    wstr *lines = wstrNewSplit(config, "\n", 1, &count);
    char *err = NULL;
    struct configuration *conf;
    void *ptr = NULL;

    int i = 0;
    if (lines == NULL) {
        err = "Memory alloc failed";
        goto loaderr;
    }
    for (i = 0; i < count; i++) {
        wstr line = wstrStrip(lines[i], "\t\n\r ");
        if (line[0] == '#' || line[0] == '\0' || line[0] == ' ')
            continue;

        wstr *argvs = wstrNewSplit(line, " ", 1, &args);
        if (argvs == NULL) {
            err = "Memory alloc failed";
            goto loaderr;
        }

        if ((conf = getConfiguration(argvs[0])) == NULL) {
            err = "Unknown configuration name";
            goto loaderr;
        }

        ptr = argvs[1];
        if (args == 1 && conf->format == LIST_FORMAT) {
            struct list *l = handleList(lines, &i, count);
            if (!l) {
                err = "parse list failed";
                goto loaderr;
            }
            ptr = l;
        } else if (args != conf->args && args == 2) {
            int j;
            for (j = 2; j < args; ++j) {
                argvs[1] = wstrCatLen(argvs[1], argvs[j], wstrlen(argvs[j]));
                if (!argvs[1]) {
                    err = "Unknown configuration name";
                    goto loaderr;
                }
            }
        }
        if (args != conf->args && conf->args != WHEAT_ARGS_NO_LIMIT) {
            err = "Incorrect args";
            goto loaderr;
        }
        if (conf->validator(conf, argvs[0], ptr) != VALIDATE_OK) {
            err = "Validate Failed";
            goto loaderr;
        }

        wstrFreeSplit(argvs, args);
    }
    wstrFreeSplit(lines, count);
    fillServerConfig();
    return ;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", i+1);
    fprintf(stderr, ">>> '%s'\n", lines[i]);
    fprintf(stderr, "Reason: %s\n", err);
    halt(1);
}

void loadConfigFile(const char *filename, char *options, int test)
{
    wstr config = wstrEmpty();
    char line[WHEATSERVER_CONFIGLINE_MAX+1];
    if (filename[0] != '\0') {
        FILE *fp;
        if ((fp = fopen(filename, "r")) == NULL) {
            wheatLog(WHEAT_WARNING,
                "Fatal error, can't open config file '%s'", filename);
            halt(1);
        }
        while(fgets(line, WHEATSERVER_CONFIGLINE_MAX, fp) != NULL)
            config = wstrCat(config, line);
        if (fp != stdin)
            fclose(fp);
    }
    if (options) {
        config = wstrCat(config,"\n");
        config = wstrCat(config, options);
    }

    applyConfig(config);
    extraValidator();
    printServerConfig(test);
    wstrFree(config);
}

// Return strings constructed by config and formatting its value by type
static ssize_t constructConfigFormat(struct configuration *conf, char *buf, size_t len)
{
    int ret = 0, pos = 0;
    struct listIterator *iter = NULL;
    wstr line = NULL;
    struct listNode *node = NULL;
    switch(conf->format) {
        case STRING_FORMAT:
            ret = snprintf(buf, len, "%s: %s", conf->name, (char *)conf->target.ptr);
            break;
        case INT_FORMAT:
            ret = snprintf(buf, len, "%s: %d", conf->name, conf->target.val);
            break;
        case ENUM_FORMAT:
            ret = snprintf(buf, len, "%s: %s", conf->name, conf->target.enum_ptr->name);
            break;
        case BOOL_FORMAT:
            ret = snprintf(buf, len, "%s: %d", conf->name, conf->target.val);
            break;
        case LIST_FORMAT:
            ret = snprintf(buf, len, "%s: ", conf->name);
            pos = ret;
            if (!conf->target.ptr)
                break;
            iter = listGetIterator((struct list*)(conf->target.ptr), START_HEAD);
            while ((node = listNext(iter)) != NULL) {
                line = (wstr)listNodeValue(node);
                ret = snprintf(buf+pos, len, "%s\t", line);
                if (ret < 0 || ret > len-pos)
                    return pos;
                pos += ret;
            }
            freeListIterator(iter);
    }
    return ret;
}

void configCommand(struct masterClient *c)
{
    struct configuration *conf = NULL;
    char buf[255];
    ssize_t len;
    conf = getConfiguration(c->argv[1]);
    if (conf == NULL) {
        len = snprintf(buf, 255, "No Correspond Configuration");
    } else {
        len = constructConfigFormat(conf, buf, 255);
    }
    replyMasterClient(c, buf, len);
}

// Used by debug and displayed under DEBUG mode
void printServerConfig(int test)
{
    struct configuration *conf;
    int level;
    struct listNode *node;
    struct listIterator *iter;
    char buf[255];

    level = test ? WHEAT_NOTICE : WHEAT_DEBUG;
    wheatLog(level, "---- Now Configuration are ----");
    iter = listGetIterator(Server.confs, START_HEAD);
    while ((node = listNext(iter)) != NULL) {
        conf = listNodeValue(node);
        constructConfigFormat(conf, buf, 255);
        wheatLog(level, "%s", buf);
    }
    freeListIterator(iter);
    wheatLog(level, "-------------------------------");
}
