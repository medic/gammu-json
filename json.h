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

#include "types.h"
#include "allocate.h"

#ifndef __JSON_H__
#define __JSON_H__

/** --- **/

#define json_argument_list_start    (128)
#define json_argument_list_maximum  (524288)

#define json_parser_tokens_start    (32)
#define json_parser_tokens_maximum  (32768)

/** --- **/

/**
 * @name parsed_json_t:
 */
typedef struct parsed_json {

  char *json;
  jsmn_parser parser;
  jsmntok_t *tokens;
  unsigned int nr_tokens;

} parsed_json_t;

/**
 * @name json_validation_state_t:
 */
typedef enum {
  START = 0, IN_ROOT_OBJECT, IN_ARGUMENTS_ARRAY, SUCCESS
} json_validation_state_t;

/**
 * @name json_validation_error_t:
 */
typedef enum {
  V_ERR_NONE = 0, V_ERR_PARSE = 1,
    V_ERR_MEM_LIMIT = 2, V_ERR_MEM_ALLOC = 3,
    V_ERR_OVERFLOW = 3, V_ERR_ROOT_TYPE = 5,
    V_ERR_PROPS_TYPE = 6, V_ERR_PROPS_ODD = 7,
    V_ERR_CMD_TYPE = 8, V_ERR_ARGS_TYPE = 9,
    V_ERR_ARG_TYPE = 10, V_ERR_ARGS_NUMERIC = 11,
    V_ERR_PROPS_MISSING = 12, V_ERR_UNKNOWN = 13
} json_validation_error_t;

/**
 * @name parsed_json_to_arguments:
 */
boolean_t parsed_json_to_arguments(parsed_json_t *p,
                                   int *argc, char **argv[], int *err);

/**
 * @name print_parsed_json:
 */
void print_parsed_json(parsed_json_t *p);

/**
 * @name parse_json:
 */
parsed_json_t *parse_json(char *json);

/**
 * @name release_parsed_json:
 */
void release_parsed_json(parsed_json_t *p);

/**
 * @name json_validation_error_text:
 */
const char *json_validation_error_text(json_validation_error_t err);

/** --- **/

#endif /* __JSON_H__ */

/* vim: set ts=4 sts=2 sw=2 expandtab: */
