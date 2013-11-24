/*
 * Copyright (c) 2010 Serge A. Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __JSMN_H__
#define __JSMN_H__

/* Feature definitions:
 *   We don't include these in $CFLAGS, as anyone including jsmn.h
 *   and linking adainst libjsmn.h would also have to define them
 *   identically.  Failure to define these macros in the same way
 *   for all code that includes jsmn.h would lead to differently
 *   sized definitions of jsmntok_t in different locations, leading
 *   to buffer overruns on the heap and a glibc call to abort(). */

#define JSMN_STRICT         (1)
#define JSMN_PARENT_LINKS   (1)

/**
 * JSON type identifier. Basic types are:
 * 	o Object
 * 	o Array
 * 	o String
 * 	o Other primitive: number, boolean (true/false) or null
 */
typedef enum {
	JSMN_PRIMITIVE = 0,
	JSMN_OBJECT = 1,
	JSMN_ARRAY = 2,
	JSMN_STRING = 3
} jsmntype_t;

typedef enum {
	/* Not enough tokens were provided */
	JSMN_ERROR_NOMEM = -1,
	/* Invalid character inside JSON string */
	JSMN_ERROR_INVAL = -2,
	/* The string is not a full JSON packet, more bytes expected */
	JSMN_ERROR_PART = -3,
	/* Everything was fine */
	JSMN_SUCCESS = 0
} jsmnerr_t;

/**
 * JSON token description.
 * @param		type	type (object, array, string etc.)
 * @param		start	start position in JSON data string
 * @param		end		end position in JSON data string
 */
typedef struct {
	jsmntype_t type;
	int start;
	int end;
	int size;
	#ifdef JSMN_PARENT_LINKS
		int parent;
	#endif
} jsmntok_t;

/**
 * JSON parser. Contains an array of token blocks available. Also stores
 * the string being parsed now and current position in that string
 */
typedef struct {
	unsigned int pos; /* offset in the JSON string */
	int toknext; /* next token to allocate */
	int toksuper; /* superior token node, e.g parent object or array */
} jsmn_parser;

/**
 * Create JSON parser over an array of tokens
 */
void jsmn_init(jsmn_parser *parser);

/**
 * Run JSON parser. It parses a JSON data string into and array of tokens, each describing
 * a single JSON object.
 */
jsmnerr_t jsmn_parse(jsmn_parser *parser, const char *js, 
		jsmntok_t *tokens, unsigned int num_tokens);

/**
 * Mark a token as invalid. This can be used prior to parsing, in
 * order to easily detect tokens that were not filled by jsmn_parse.
 */
void jsmn_mark_token_invalid(jsmntok_t *token);

/**
 * Return a true value if the token supplied is in the "invalid"
 * state; return false if the token contains any kind of parsed data.
 */
int jsmn_token_is_invalid(jsmntok_t *t);

/**
 * Destructively translate a jsmn token in to a null-terminated string.
 * See jsmn.c for a full description.
 */
char *jsmn_stringify_token(char *json, jsmntok_t *token);

#endif /* __JSMN_H__ */

/* vim: set ts=4 sts=0 noexpandtab: */

