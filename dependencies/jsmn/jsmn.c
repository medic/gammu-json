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

#include <stdlib.h>
#include "jsmn.h"

/**
 * Mark a token as invalid. This can be used prior to parsing, in
 * order to easily detect tokens that were not filled by jsmn_parse.
 */
void jsmn_mark_token_invalid(jsmntok_t *t) {

	t->start = t->end = -1;
	t->size = 0;

	#ifdef JSMN_PARENT_LINKS
		t->parent = -1;
	#endif
}

/**
 * Return a true value if the token supplied is in the "invalid"
 * state; return false if the token contains any kind of parsed data.
 */
int jsmn_token_is_invalid(jsmntok_t *t) {

	return (t->start == -1 || t->end == -1);
}

/**
 * Allocates a fresh unused token from the token pool.
 */
static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, 
								   jsmntok_t *tokens, size_t num_tokens) {

	jsmntok_t *tok;
	if (parser->toknext >= num_tokens) {
		return NULL;
	}
	tok = &tokens[parser->toknext++];
	jsmn_mark_token_invalid(tok);
	return tok;
}

/**
 * Fills token type and boundaries.
 */
static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type, 
							int start, int end) {
	token->type = type;
	token->start = start;
	token->end = end;
	token->size = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static jsmnerr_t jsmn_parse_primitive(jsmn_parser *parser, const char *js,
									  jsmntok_t *tokens, size_t num_tokens) {

	jsmntok_t *token;
	int start;

	start = parser->pos;

	for (; js[parser->pos] != '\0'; parser->pos++) {
		switch (js[parser->pos]) {
			#ifndef JSMN_STRICT
				/* Strict mode:
				    Primitive must be followed by "," or "}" or "]" */
				case ':':
			#endif
			case '\t' : case '\r' : case '\n' : case ' ' :
			case ','  : case ']'  : case '}' :
				goto found;
		}
		if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
			parser->pos = start;
			return JSMN_ERROR_INVAL;
		}
	}
	#ifdef JSMN_STRICT
		/* Strict mode:
		    Primitive must be followed by a comma/object/array */
		parser->pos = start;
		return JSMN_ERROR_PART;
	#endif

	found:
		token = jsmn_alloc_token(parser, tokens, num_tokens);
		if (token == NULL) {
			parser->pos = start;
			return JSMN_ERROR_NOMEM;
		}
		jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
		#ifdef JSMN_PARENT_LINKS
			token->parent = parser->toksuper;
		#endif
		parser->pos--;
		return JSMN_SUCCESS;
}

/**
 * Filsl next token with JSON string.
 */
static jsmnerr_t jsmn_parse_string(jsmn_parser *parser, const char *js,
								   jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;
	int start = parser->pos;

	parser->pos++;

	/* Skip starting quote */
	for (; js[parser->pos] != '\0'; parser->pos++) {
		char c = js[parser->pos];

		/* Quote: end of string */
		if (c == '\"') {
			token = jsmn_alloc_token(parser, tokens, num_tokens);
			if (token == NULL) {
				parser->pos = start;
				return JSMN_ERROR_NOMEM;
			}
			jsmn_fill_token(token, JSMN_STRING, start+1, parser->pos);
			#ifdef JSMN_PARENT_LINKS
				token->parent = parser->toksuper;
			#endif
			return JSMN_SUCCESS;
		}

		/* Backslash: Quoted symbol expected */
		if (c == '\\') {
			parser->pos++;
			switch (js[parser->pos]) {
				/* Allowed escaped symbols */
				case '\"': case '/' : case '\\' : case 'b' :
				case 'f' : case 'r' : case 'n'  : case 't' :
					break;
				/* Allows escaped symbol \uXXXX */
				case 'u':
					parser->pos++;
					int i = 0;
					for(;i<4&&js[parser->pos] != '\0';i++) {
						/* If it isn't a hex character, we have an error */
						if(!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || // 0-9
								(js[parser->pos] >= 65 && js[parser->pos] <= 70) || // A-F
								(js[parser->pos] >= 97 && js[parser->pos] <= 102))) { // a-f
							parser->pos = start;
							return JSMN_ERROR_INVAL;
						}
						parser->pos++;
					}
					parser->pos--;
					break;
				/* Unexpected symbol */
				default:
					parser->pos = start;
					return JSMN_ERROR_INVAL;
			}
		}
	}
	parser->pos = start;
	return JSMN_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
jsmnerr_t jsmn_parse(jsmn_parser *parser, const char *js,
					 jsmntok_t *tokens, unsigned int num_tokens) {
	jsmnerr_t r;
	int i;
	jsmntok_t *token;

	for (; js[parser->pos] != '\0'; parser->pos++) {
		char c;
		jsmntype_t type;

		c = js[parser->pos];
		switch (c) {
			case '{': case '[':
				token = jsmn_alloc_token(parser, tokens, num_tokens);
				if (token == NULL) {
					return JSMN_ERROR_NOMEM;
				}
				if (parser->toksuper != -1) {
					tokens[parser->toksuper].size++;
					#ifdef JSMN_PARENT_LINKS
						token->parent = parser->toksuper;
					#endif
				}
				token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
				token->start = parser->pos;
				parser->toksuper = parser->toknext - 1;
				break;
			case '}': case ']':
				type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
				#ifdef JSMN_PARENT_LINKS
					if (parser->toknext < 1) {
						return JSMN_ERROR_INVAL;
					}
					token = &tokens[parser->toknext - 1];
					for (;;) {
						if (token->start != -1 && token->end == -1) {
							if (token->type != type) {
								return JSMN_ERROR_INVAL;
							}
							token->end = parser->pos + 1;
							parser->toksuper = token->parent;
							break;
						}
						if (token->parent == -1) {
							break;
						}
						token = &tokens[token->parent];
				}
				#else
					for (i = parser->toknext - 1; i >= 0; i--) {
						token = &tokens[i];
						if (token->start != -1 && token->end == -1) {
							if (token->type != type) {
								return JSMN_ERROR_INVAL;
							}
							parser->toksuper = -1;
							token->end = parser->pos + 1;
							break;
						}
					}
					/* Error if unmatched closing bracket */
					if (i == -1) {
						return JSMN_ERROR_INVAL;
					}
					for (; i >= 0; i--) {
						token = &tokens[i];
						if (token->start != -1 && token->end == -1) {
							parser->toksuper = i;
							break;
						}
					}
				#endif
				break;
			case '\"':
				r = jsmn_parse_string(parser, js, tokens, num_tokens);
				if (r < 0) {
					return r;
				}
				if (parser->toksuper != -1) {
					tokens[parser->toksuper].size++;
				}
				break;
			case '\t' : case '\r' : case '\n' : case ':' : case ',': case ' ': 
				break;
			#ifdef JSMN_STRICT
				/* Strict mode:
				    Primitives are numbers and booleans */
				case '-': case '0': case '1' : case '2': case '3' : case '4':
				case '5': case '6': case '7' : case '8': case '9':
				case 't': case 'f': case 'n' :
			#else
				/* Non-strict mode:
				    Every unquoted value is a primitive */
				default:
			#endif
					r = jsmn_parse_primitive(parser, js, tokens, num_tokens);
					if (r < 0) {
						return r;
					}
					if (parser->toksuper != -1) {
						tokens[parser->toksuper].size++;
					}
					break;

			#ifdef JSMN_STRICT
				/* Unexpected char in strict mode */
				default:
					return JSMN_ERROR_INVAL;
			#endif

		}
	}

	for (i = parser->toknext - 1; i >= 0; i--) {
		/* Unmatched opened object or array */
		if (tokens[i].start != -1 && tokens[i].end == -1) {
			return JSMN_ERROR_PART;
		}
	}

	return JSMN_SUCCESS;
}

/**
 * Creates a new parser based over a given buffer with an array of tokens 
 * available.
 */
void jsmn_init(jsmn_parser *parser) {

	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
}

/**
 * Translate a jsmn token in to a null-terminated string. Returns a pointer to
 * a null-terminated string that *overlaps* the original JSON string. It is not
 * necessary to free the returned value; pointers returned from this function
 * will be invalidated when the original JSON string is freed. This function is
 * destructive, in that it modifies the original JSON string rendering it
 * unparsable in the future. If there is no string data associated with the
 * token you provide, this function returns NULL and has no side-effects.
 */
char *jsmn_stringify_token(char *json, jsmntok_t *token) {

	if (token->type != JSMN_PRIMITIVE && token->type != JSMN_STRING) {
		return NULL;
	}

	json[token->end] = '\0';
	return &json[token->start];
}

/* vim: set ts=4 sts=0 noexpandtab: */

