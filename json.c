/**
 * gammu-json
 *
 * Copyright (c) 2013-2014 David Brown <hello at scri.pt>.
 * Copyright (c) 2013-2014 Medic Mobile, Inc. <david at medicmobile.org>
 *
 * All rights reserved.
 *
 * This is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version three,
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of version three of the GNU General
 * Public License along with this software. If you did not, see
 * http://www.gnu.org/licenses/.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DAVID BROWN OR
 * MEDIC MOBILE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <jsmn.h>
#include <ctype.h>
#include <string.h>

#include "json.h"

/** --- **/

#define json_argument_list_start    (128)
#define json_argument_list_maximum  (524288)

#define json_parser_tokens_start    (32)
#define json_parser_tokens_maximum  (32768)

/** --- **/

/**
 * @name json_validation_errors:
 */
static const char *const json_validation_errors[] = {
  /* 0 */  "success; no error",
  /* 1 */  "parse error: invalid or malformed JSON",
  /* 2 */  "parser memory limit exceeded",
  /* 3 */  "internal error: memory allocation failure",
  /* 4 */  "internal error: integer value would overflow",
  /* 5 */  "root entity must be an object",
  /* 6 */  "property names must be strings",
  /* 7 */  "object contains one or more incomplete key/value pairs",
  /* 8 */  "value for the `command` property must be a string",
  /* 9 */  "value for `arguments` property must be an array",
  /* 10 */ "arguments must be either strings or numeric values",
  /* 11 */ "non-string values in `arguments` must be numeric",
  /* 12 */ "one or more required properties are missing"
};

/**
 * @name parsed_json_to_arguments:
 */
boolean_t parsed_json_to_arguments(parsed_json_t *p,
                                   int *argc, char **argv[], int *err) {

  int n = 0;
  char **rv = NULL;

  unsigned int size = 0;
  jsmntok_t *tokens = p->tokens;
  json_validation_state_t state = START;
  boolean_t matched_keys[] = { FALSE, FALSE };
  unsigned int object_size = 0, array_size = 0;

  #define return_validation_error(e) \
    do { *err = (e); goto validation_error; } while (0)

  #define token_lookahead(i, c) \
    (((i) + (c)) < p->nr_tokens && \
      !jsmn_token_is_invalid(tokens + (i) + (c)) ? \
        (tokens + (i) + (c)) : NULL)

  /* For every token */
  for (unsigned int i = 0; i < p->nr_tokens; ++i) {

    jsmntok_t *t = &tokens[i];

    if (matched_keys[0] && matched_keys[1]) {
      state = SUCCESS;
      break;
    }

    if (jsmn_token_is_invalid(t)) {
      break;
    }

    if (!size || n >= size) {

      /* Increase size by a few orders of magnitude */
      size = (size ? size * 8 : json_argument_list_start);

      /* Increase size, then check against memory limit */
      if (size > json_argument_list_maximum) {
        return_validation_error(V_ERR_MEM_LIMIT);
      }

      /* Enlarge array of argument pointers */
      if (!(rv = (char **) reallocate_array(rv, sizeof(char *), size, 1))) {
        return_validation_error(V_ERR_MEM_ALLOC);
      }
    }

    switch (state) {

      case START: {

        if (t->type != JSMN_OBJECT) {
          return_validation_error(V_ERR_ROOT_TYPE);
        }

        if (t->size % 2 != 0) {
          return_validation_error(V_ERR_PROPS_ODD);
        }

        object_size = t->size;
        state = IN_ROOT_OBJECT;

        break;
      }

      case IN_ROOT_OBJECT: {

        if (object_size <= 0) {
          break;
        }

        if (t->type != JSMN_STRING) {
          return_validation_error(V_ERR_PROPS_TYPE);
        }

        char *s = jsmn_stringify_token(p->json, t);

        if (!(t = token_lookahead(i, 1))) {
          return_validation_error(V_ERR_PROPS_ODD);
        }

        if (strcmp(s, "command") == 0) {

          if (t->type != JSMN_STRING) {
            return_validation_error(V_ERR_CMD_TYPE);
          }

          rv[0] = jsmn_stringify_token(p->json, t);
          matched_keys[0] = TRUE;

        } else if (strcmp(s, "arguments") == 0) {

          if (t->type != JSMN_ARRAY && t->type != JSMN_OBJECT) {
            return_validation_error(V_ERR_ARGS_TYPE);
          }

          /* Handle empty arrays */
          jsmntok_t *tt = token_lookahead(i, 2);

          if (!tt || t->size <= 0) {
            matched_keys[1] = TRUE;
          } else {
            state = IN_ARGUMENTS_ARRAY;
          }

          /* Enter array */
          array_size = t->size;
        }

        /* To walk the stair / steps in pairs */
        object_size -= 2;
        i++;

        break;
      }

      case IN_ARGUMENTS_ARRAY: {

        if (t->type != JSMN_PRIMITIVE && t->type != JSMN_STRING) {
          return_validation_error(V_ERR_ARG_TYPE);
        }

        char *s = jsmn_stringify_token(p->json, t);

        /* Require that primitives are numeric */
        if (t->type == JSMN_PRIMITIVE && (!s || !isdigit(s[0]))) {
          return_validation_error(V_ERR_ARGS_NUMERIC);
        }

        rv[++n] = s;

        if (--array_size <= 0) {
          matched_keys[1] = TRUE;
          state = IN_ROOT_OBJECT;
        }

        break;
      }

      case SUCCESS: {
        goto successful;
      }

      default: {
        return_validation_error(V_ERR_UNKNOWN);
        break;
      }
    }
  }

  if (state != SUCCESS) {
    return_validation_error(V_ERR_PROPS_MISSING);
  }

  /* Victory */
  successful:

    /* Null-terminate */
    rv[n + 2] = NULL;

    /* Return values */
    *argv = rv;
    *argc = n + 1;

    /* Success */
    return TRUE;

  /* Non-victory */
  validation_error:

    if (rv) {
      free(rv);
    }

    return FALSE;
}

/**
 * @name print_parsed_json:
 */
void print_parsed_json(parsed_json_t *p) {

  debug("start\n");

  for (unsigned int i = 0; i < p->nr_tokens; ++i) {

    jsmntok_t *t = &p->tokens[i];

    if (jsmn_token_is_invalid(t)) {
      debug("end\n");
      break;
    }
    
    char *s = jsmn_stringify_token(p->json, t);

    switch (t->type) {
      case JSMN_STRING:
        debug("string: '%s'\n", s);
        break;
      case JSMN_PRIMITIVE:
        debug("primitive: '%s'\n", s);
        break;
      case JSMN_OBJECT:
        debug("object[%d]\n", t->size);
        break;
      case JSMN_ARRAY:
        debug("array[%d]\n", t->size);
        break;
      default:
        debug("unknown-%d[%d]: '%s'\n", t->type, t->size, s);
        continue;
    }
  }
}

/**
 * @name parse_json:
 */
parsed_json_t *parse_json(char *json) {

  parsed_json_t *rv =
    (parsed_json_t *) allocate(sizeof(parsed_json_t));

  if (!rv) {
    return NULL;
  }

  rv->json = json;
  rv->tokens = NULL;
  rv->nr_tokens = 0;

  unsigned int n = json_parser_tokens_start;

  for (;;) {

    /* Check against upper limit */
    if (n > json_parser_tokens_maximum) {
      goto allocation_error;
    }

    jsmn_init(&rv->parser);

    rv->tokens =
      reallocate_array(rv->tokens, sizeof(jsmntok_t), n, 0);

    if (!rv->tokens) {
      goto allocation_error;
    }

    /* Set all tokens to invalid */
    for (unsigned int i = 0; i < n; ++i) {
      jsmn_mark_token_invalid(&rv->tokens[i]);
    }

    /* Parse */
    jsmnerr_t result =
      jsmn_parse(&rv->parser, json, rv->tokens, n);

    /* Not enough room to parse the full string?
     *   Increase the available token space by a couple (base two)
     *   orders of magnitude, then go around, reallocate, and retry. */

    if (result == JSMN_ERROR_NOMEM) {
      n *= 4;
      continue;
    }

    if (result < 0) {
      goto allocation_error;
    }

    /* Parsed successfully */
    rv->nr_tokens = n;
    break;
  }

  /* Success */
  return rv;

  /* Error:
   *  Clean up the `rv` structure and its members. */

  allocation_error:

    if (rv->tokens) {
      free(rv->tokens);
    }

    free(rv);
    return NULL;
}

/**
 * @name release_parsed_json:
 */
void release_parsed_json(parsed_json_t *p) {

  free(p->tokens);
  free(p);
}

/**
 * @name json_validation_error_text:
 */
const char *json_validation_error_text(json_validation_error_t err) {

  return (
    (err < V_ERR_UNKNOWN) ?
      json_validation_error_text(err) : "unknown or unhandled error"
  );
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */
