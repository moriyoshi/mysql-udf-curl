/*
 * cURL UDF 
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <my_global.h>
#include <my_sys.h>
#include <m_ctype.h>
#include <m_string.h>		// To get strmov()
#include <mysql.h>

#include <limits.h>
#include <assert.h>
#include <curl/curl.h>

#ifdef HAVE_DLOPEN

/* These must be right or mysqld will not find the symbol! */

#ifdef __cplusplus
extern "C" {
#endif

static pthread_once_t thr_init_once = PTHREAD_ONCE_INIT;
static pthread_key_t thr_ctx_key;

struct thr_ctx {
    CURL *curl;
};

static void thr_ctx_fini(struct thr_ctx *ctx)
{
    if (!ctx)
        return;
    if (ctx->curl)
        curl_easy_cleanup(ctx->curl);
    free(ctx);
}

static int thr_ctx_init(struct thr_ctx *ctx)
{
    ctx->curl = curl_easy_init();
    if (!ctx->curl)
        return 1;
    return 0;
}

static void global_init(void)
{
    if (pthread_key_create(&thr_ctx_key, (void(*)(void*))&thr_ctx_fini)) abort();
}

static struct thr_ctx *thr_ctx(void)
{
    struct thr_ctx *ctx;
    if (pthread_once(&thr_init_once, global_init)) abort();
    ctx = pthread_getspecific(thr_ctx_key);
    if (ctx)
        return ctx;
    ctx = malloc(sizeof(*ctx));
    if (!ctx) abort(); /* must not happen */
    if (thr_ctx_init(ctx)) abort();
    if (pthread_setspecific(thr_ctx_key, ctx)) abort();
    return ctx;
}

struct readdata_handler_ctx {
    const char *buf;
    const char *buf_end;
    const char *p;
};

static size_t readdata(void *ptr, size_t size, size_t nmemb, void *stream)
{
    struct readdata_handler_ctx *ctx = stream;
    size_t nbytescopy;

    assert(size == 1);

    nbytescopy = ctx->buf_end - ctx->p;
    if (nbytescopy > nmemb)
        nbytescopy = nmemb;
    memcpy(ptr, ctx->p, nbytescopy);
    ctx->p += nbytescopy;
    return nbytescopy;
}

struct writedata_handler_ctx {
    char *buf;
    size_t buf_cap;
    size_t buf_len;
};

static size_t writedata(void *ptr, size_t size, size_t nmemb, void *stream)
{
    struct writedata_handler_ctx *ctx = stream;
    size_t req_size;

    assert(size == 1);

    req_size = ctx->buf_len + nmemb;
    if (req_size >= ctx->buf_cap) {
        char *new_buf;
        size_t new_buf_cap = ctx->buf_cap ? ctx->buf_cap: 16;
        do {
            new_buf_cap = new_buf_cap + (new_buf_cap >> 1);
        } while (new_buf_cap <= req_size);
        new_buf = my_realloc(ctx->buf, new_buf_cap, MYF(MY_WME));
        if (!new_buf)
            return 0;
        ctx->buf = new_buf;
        ctx->buf_cap = new_buf_cap;
    }

    memcpy(ctx->buf + ctx->buf_len, ptr, nmemb);
    ctx->buf_len += nmemb;
    ctx->buf[ctx->buf_len] = '\0';
    return nmemb;
}

/**
 * @param initid	Points to a structure that the init function should fill.
 *		This argument is given to all other functions.
 *		my_bool maybe_null	1 if function can return NULL
 *				Default value is 1 if any of the arguments
 *				is declared maybe_null.
 *		unsigned int decimals	Number of decimals.
 *				Default value is max decimals in any of the
 *				arguments.
 *		unsigned int max_length  Length of string result.
 *				The default value for integer functions is 21
 *				The default value for real functions is 13+
 *				default number of decimals.
 *				The default value for string functions is
 *				the longest string argument.
 *		char *ptr;		A pointer that the function can use.
 *
 * @param args		Points to a structure which contains:
 *		unsigned int arg_count		Number of arguments
 *		enum Item_result *arg_type	Types for each argument.
 *					Types are STRING_RESULT, REAL_RESULT
 *					and INT_RESULT.
 *		char **args			Pointer to constant arguments.
 *					Contains 0 for not constant argument.
 *		unsigned long *lengths;		max string length for each argument
 *		char *maybe_null		Information of which arguments
 *					may be NULL
 *
 * @param message	Error message that should be passed to the user on fail.
 *		The message buffer is MYSQL_ERRMSG_SIZE big, but one should
 *		try to keep the error message less than 80 bytes long!
 *
 * This function should return 1 if something goes wrong. In this case
 * message should contain something usefull!
 */
my_bool curl_fetch_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
	int i;
    struct thr_ctx *ctx = thr_ctx();
    initid->ptr = (void*)ctx;

	if (args->arg_count < 1) {
		strncpy(message, "curl_fetch requires at least one argument",
			    MYSQL_ERRMSG_SIZE);
		return 1;	
	} else if (args->arg_count > 3) {
		strncpy(message, "curl_fetch requires at most three argument",
				MYSQL_ERRMSG_SIZE);
		return 1;	
    }

	for (i = 0; i < args->arg_count; i++) {
		args->arg_type[i] = STRING_RESULT;
		args->maybe_null[i] = 0;
	}

	initid->max_length = UINT_MAX;

    {
        const char *method = "get";
        if (args->arg_count >= 2) {
            method = args->args[1];
            if (!method) {
                strncpy(message, "argument 2 must be constant", MYSQL_ERRMSG_SIZE);
                return 1;
            }
        }

        if (my_strcasecmp_8bit(&my_charset_latin1, method, "post") == 0) {
            if (args->arg_count < 3) {
                strncpy(message, "too few arguments", MYSQL_ERRMSG_SIZE);
                return 1;
            }
            curl_easy_setopt(ctx->curl, CURLOPT_POST, 1);
        } else if (my_strcasecmp_8bit(&my_charset_latin1, method, "put") == 0) {
            if (args->arg_count < 3) {
                strncpy(message, "too few arguments", MYSQL_ERRMSG_SIZE);
                return 1;
            }
            curl_easy_setopt(ctx->curl, CURLOPT_UPLOAD, 1);
        } else if (my_strcasecmp_8bit(&my_charset_latin1, method, "get") != 0) {
            my_snprintf_8bit(&my_charset_latin1, message, MYSQL_ERRMSG_SIZE, "invalid method name `%s'", method);
            return 1;
        }
    } 

    curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, writedata);
    curl_easy_setopt(ctx->curl, CURLOPT_READFUNCTION, readdata);

	return 0;
}

/**
 * Deinit function. This should free all resources allocated by
 * this function.
 * @param initid	Return value from xxxx_init
 */
void curl_fetch_deinit(UDF_INIT *initid)
{
}

/**
 * URL fetch function.
 * @param initid  Structure filled by xxx_init
 * @param args    The same structure as to xxx_init. This structure
 *	              contains values for all parameters.
 *
 *                Note that the functions MUST check and convert all
 *                to the type it wants!  Null values are represented by
 *                a NULL pointer
 * @param result  Possible buffer to save result. At least 255 byte long.
 * @param length  Pointer to length of the above buffer.
 *                In this the function should save the result length
 * @param is_null If the result is null, one should store 1 here.
 * @param error   If something goes fatally wrong one should store 1 here.
 *
 * @return This function should return a pointer to the result string.
 *         Normally this is 'result' but may also be an alloced string.
 */
char *curl_fetch(UDF_INIT *initid, UDF_ARGS *args, char *result,
		unsigned long *length, char *is_null, char *error)
{
    struct thr_ctx *ctx = (struct thr_ctx *)initid->ptr;
    CURLcode status;
    void *prev_readdata, *prev_writedata;
    struct writedata_handler_ctx wctx = { 0, 0, 0 };
    struct readdata_handler_ctx rctx = {
        args->args[2],
        args->args[2] + args->lengths[2],
        args->args[2]
    };
    curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(ctx->curl, CURLOPT_READDATA, &rctx);
    curl_easy_setopt(ctx->curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ctx->curl, CURLOPT_URL, args->args[0]);

    status = curl_easy_perform(ctx->curl);

    if (status != CURLE_OK) {
        *is_null = 1;
        *error = 1; 
        return 0;
    }

    *length = wctx.buf_len;
	return wctx.buf;
}

/**
 * @param initid	Points to a structure that the init function should fill.
 *		This argument is given to all other functions.
 *		my_bool maybe_null	1 if function can return NULL
 *				Default value is 1 if any of the arguments
 *				is declared maybe_null.
 *		unsigned int decimals	Number of decimals.
 *				Default value is max decimals in any of the
 *				arguments.
 *		unsigned int max_length  Length of string result.
 *				The default value for integer functions is 21
 *				The default value for real functions is 13+
 *				default number of decimals.
 *				The default value for string functions is
 *				the longest string argument.
 *		char *ptr;		A pointer that the function can use.
 *
 * @param args		Points to a structure which contains:
 *		unsigned int arg_count		Number of arguments
 *		enum Item_result *arg_type	Types for each argument.
 *					Types are STRING_RESULT, REAL_RESULT
 *					and INT_RESULT.
 *		char **args			Pointer to constant arguments.
 *					Contains 0 for not constant argument.
 *		unsigned long *lengths;		max string length for each argument
 *		char *maybe_null		Information of which arguments
 *					may be NULL
 *
 * @param message	Error message that should be passed to the user on fail.
 *		The message buffer is MYSQL_ERRMSG_SIZE big, but one should
 *		try to keep the error message less than 80 bytes long!
 *
 * This function should return 1 if something goes wrong. In this case
 * message should contain something usefull!
 */
my_bool curl_esc_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
	int i;
    CURL *curl;

    initid->ptr = (void *)thr_ctx();

	if (args->arg_count != 1) {
		strncpy(message, "curl_fetch requires exactly one argument",
			    MYSQL_ERRMSG_SIZE);
		return 1;
    }

    args->arg_type[0] = STRING_RESULT;
    args->maybe_null[i] = 0;

	initid->max_length = UINT_MAX;
	return 0;
}

/**
 * Deinit function. This should free all resources allocated by
 * this function.
 * @param initid	Return value from xxxx_init
 */
void curl_esc_deinit(UDF_INIT *initid)
{
}

/**
 * URL escape function.
 * @param initid  Structure filled by xxx_init
 * @param args    The same structure as to xxx_init. This structure
 *	              contains values for all parameters.
 *
 *                Note that the functions MUST check and convert all
 *                to the type it wants!  Null values are represented by
 *                a NULL pointer
 * @param result  Possible buffer to save result. At least 255 byte long.
 * @param length  Pointer to length of the above buffer.
 *                In this the function should save the result length
 * @param is_null If the result is null, one should store 1 here.
 * @param error   If something goes fatally wrong one should store 1 here.
 *
 * @return This function should return a pointer to the result string.
 *         Normally this is 'result' but may also be an alloced string.
 */
char *curl_esc(UDF_INIT *initid, UDF_ARGS *args, char *result,
		unsigned long *length, char *is_null, char *error)
{
    char *_result = curl_easy_escape(((struct thr_ctx *)initid->ptr)->curl,
            args->args[0], args->lengths[0]);
    size_t _result_len;
    if (!_result) {
        *is_null = 1;
        *error = 1;
        return 0;
    }

    _result_len = strlen(_result);
    if (_result_len > *length) {
        result = my_malloc(_result_len, MYF(MY_WME));
        if (!result) {
            *is_null = 1;
            *error = 1;
            return 0;
        }
    }
    memcpy(result, _result, _result_len);
    *length = _result_len;
	return result;
}

#ifdef __cplusplus
}
#endif

#endif /* HAVE_DLOPEN */
