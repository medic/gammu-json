/**
 * gammu-json
 *
 * Copyright (c) 2013 David Brown <hello at scri.pt>.
 * Copyright (c) 2013 Medic Mobile, Inc. <david at medicmobile.org>
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

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <inttypes.h>

#include <gammu.h>
#include <jsmn.h>

#define TIMESTAMP_MAX_WIDTH (64)
#define BITFIELD_CELL_WIDTH (CHAR_BIT)

const static char *usage_text = (
  "\n"
  "Usage:\n"
  "  %s [global-options] [command] [args]...\n"
  "\n"
  "Global options:\n"
  "\n"
  "  -c, --config <file>       Specify path to Gammu configuration file\n"
  "                            (default: /etc/gammurc).\n"
  "\n"
  "  -h, --help                Print this helpful message.\n"
  "\n"
  "  -r, --repl                Run in `read, evaluate, print' loop mode.\n"
  "                            Read a single-line JSON-encoded command\n"
  "                            from stdin, execute the command, then\n"
  "                            print its result as a single line of JSON\n"
  "                            on stdout. Repeat this until end-of-file is\n"
  "                            reached on stdin. If a command is provided\n"
  "                            via command-line arguments, execute it before\n"
  "                            attempting to read more commands from stdin.\n"
  "\n"
  "  -v, --verbose             Ask Gammu to print debugging information\n"
  "                            to stderr while performing operations.\n"
  "\n"
  "Commands:\n"
  "\n"
  "  retrieve                  Retrieve all messages from a device, as a\n"
  "                            JSON-encoded array of objects, on stdout.\n"
  "\n"
  "  delete { all | N... }     Delete one or more messages from a device,\n"
  "                            using location numbers to identify them.\n"
  "                            Specify `all' to delete any messages found.\n"
  "                            Prints JSON-encoded information about any\n"
  "                            deleted/skipped/missing messages on stdout.\n"
  "\n"
  "  send { phone text }...    Send one or more messages. Each message is\n"
  "                            sent to exactly one phone number. Prints\n"
  "                            JSON-encoded information about the sent\n"
  "                            messages on stdout.\n"
  "About:\n"
  "\n"
  "  Copyright (c) 2013 David Brown <hello at scri.pt>.\n"
  "  Copyright (c) 2013 Medic Mobile, Inc. <david at medicmobile.org>\n"
  "\n"
  "  Released under the GNU General Public License, version three.\n"
  "  For more information, see <http://github.com/browndav/gammu-json>.\n"
  "\n"
);

/**
 * @name boolean_t
 */
typedef uint8_t boolean_t;

/**
 * @name gammu_state_t
 */
typedef struct app_options {

  boolean_t help;
  boolean_t repl;
  boolean_t invalid;
  boolean_t verbose;
  char *application_name;
  char *gammu_configuration_path;

} app_options_t;

/**
 * @name gammu_state_t
 */
typedef struct gammu_state {

  int err;
  GSM_StateMachine *sm;

} gammu_state_t;

/**
 * @name multimessage_t
 */
typedef GSM_SMSMessage message_t;

/**
 * @name multimessage_t
 */
typedef GSM_MultiSMSMessage multimessage_t;

/**
 * @name multimessage_info_t
 */
typedef GSM_MultiPartSMSInfo multimessage_info_t;

/**
 * @name message_timestamp_t
 */
typedef GSM_DateTime message_timestamp_t;

/**
 * @name smsc_t
 */
typedef GSM_SMSC smsc_t;

/**
 * @name message_iterate_fn_t
 */
typedef boolean_t (*message_iterate_fn_t)(
  gammu_state_t *, multimessage_t *, boolean_t, void *
);

/**
 * @name bitfield_t
 */
typedef struct bitfield {

  uint8_t *data;
  unsigned int n;
  unsigned int total_set;

} bitfield_t;

/**
 * @name utf8_info_t
 */
typedef struct utf8_length_info {

  unsigned int bytes;
  unsigned int symbols;

} utf8_length_info_t;

/**
 * @name part_transmit_status_t
 */
typedef struct part_transmit_status {

  int status;
  int reference;
  const char *err;
  boolean_t transmitted;

} part_transmit_status_t;

/**
 * @name transmit_status_t
 */
typedef struct transmit_status {

  const char *err;
  boolean_t finished;

  int parts_sent;
  int parts_total;
  int message_index;
  int message_part_index;
  part_transmit_status_t parts[GSM_MAX_MULTI_SMS];

} transmit_status_t;

/**
 * @name delete_status_t
 */
typedef struct delete_status {

  boolean_t is_start;
  bitfield_t *bitfield;

  unsigned int requested;
  unsigned int examined;
  unsigned int skipped;
  unsigned int attempted;
  unsigned int errors;
  unsigned int deleted;

} delete_status_t;

/**
 * @name delete_stage_t
 */
typedef enum {

  DELETE_EXAMINING = 1,
  DELETE_ATTEMPTING,
  DELETE_RESULT_BARRIER = 32,
  DELETE_SUCCESS,
  DELETE_SKIPPED,
  DELETE_ERROR

} delete_stage_t;

/**
 * @name delete_stage_t
 */
typedef struct parsed_json {

  char *json;
  jsmn_parser parser;
  jsmntok_t *tokens;
  unsigned int nr_tokens;

} parsed_json_t;


/**
 * @name delete_callback_fn_t
 */
typedef void (*delete_callback_fn_t)(
  gammu_state_t *, message_t *, delete_stage_t, void *
);

/** --- **/

static app_options_t app; /* global */

/** --- **/

inline int multiplication_will_overflow(size_t n, size_t s) {

  return (((size_t) -1 / s) < n);
}

/**
 * @name malloc_and_zero
 */
static void *malloc_and_zero(int size) {

  void *rv = malloc(size);

  if (!rv) {
    fprintf(stderr, "Fatal error: failed to allocate %d bytes\n", size);
    exit(111);
  }

  memset(rv, '\0', size);
  return rv;
}

/* --- */

#define json_argument_list_start    (128)
#define json_argument_list_maximum  (524288)

/**
 * @name json_validation_state_t:
 */
typedef enum {
  START = 0, IN_ROOT_OBJECT, IN_ARGUMENTS_ARRAY, SUCCESS
} json_validation_state_t;

/**
 * @name json_validation_errors:
 */
const char *const json_validation_errors[] = {
  /* 0 */  "success; no error",
  /* 1 */  "parser memory limit exceeded",
  /* 2 */  "internal error: memory allocation failure",
  /* 3 */  "internal error: integer value would overflow",
  /* 4 */  "root entity must be an object",
  /* 5 */  "property names must be strings",
  /* 6 */  "object contains one or more incomplete key/value pairs",
  /* 7 */  "value for the `command` property must be a string",
  /* 8 */  "value for `arguments` property must be an array",
  /* 9 */  "arguments must be either strings or numeric values",
  /* 10 */ "non-string values in `arguments` must be numeric",
  /* 11 */ "one or more required properties are missing",
  /* 12 */ "unknown or unhandled error"
};

/**
 * @name json_validation_error_to_string:
 */
const char *json_validation_error_to_string(int err) {

  if (err >= 0 && err < sizeof(json_validation_errors)) {
    return json_validation_errors[err];
  } else {
    return json_validation_errors[
      (sizeof(json_validation_errors) / sizeof(const char *)) - 1
    ];
  }
}

/**
 * @name parsed_json_to_arguments:
 */
boolean_t parsed_json_to_arguments(parsed_json_t *p,
                                   int *argc, char **argv[], int *err) {

  int n = 0;
  char **rv = NULL;

  unsigned int size = 0;
  unsigned int object_size = 0;
  jsmntok_t *tokens = p->tokens;
  json_validation_state_t state = START;

  #define return_validation_error(e) \
    do { *err = (e); goto validation_error; } while (0)

  #define token_lookahead(i, c) \
    (((i) + (c)) < p->nr_tokens && \
      !jsmn_token_is_invalid(tokens + (i) + (c)) ? \
        (tokens + (i) + (c)) : NULL)

  /* For every token */
  for (unsigned int i = 0; i < p->nr_tokens; ++i) {

    jsmntok_t *t = &tokens[i];

    if (state == SUCCESS || jsmn_token_is_invalid(t)) {
      break;
    }

    if (!size || n >= size) {

      /* Increase size by a few orders of magnitude */
      size = (size ? size * 4 : json_argument_list_start);

      /* Increase size, then check against memory limit */
      if (size > json_argument_list_maximum) {
        return_validation_error(1);
      }

      /* Paranoia: check for overflow */
      if (multiplication_will_overflow(size, sizeof(char *))) {
        return_validation_error(2);
      }

      /* Enlarge array of argument pointers */
      rv = (char **) realloc(rv, size * sizeof(char *));

      if (!rv) {
        return_validation_error(3);
      }
    }

    switch (state) {

      case START: {

        if (t->type != JSMN_OBJECT) {
          return_validation_error(4);
        }

        if (t->size % 2 != 0) {
          return_validation_error(6);
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
          return_validation_error(5);
        }

        char *s = jsmn_stringify_token(p->json, t);

        if (!(t = token_lookahead(i, 1))) {
          return_validation_error(6);
        }

        if (strcmp(s, "command") == 0) {

          if (t->type != JSMN_STRING) {
            return_validation_error(7);
          }

          rv[0] = jsmn_stringify_token(p->json, t);

          /* To walk the stair... */
          object_size -= 2;

        } else if (strcmp(s, "arguments") == 0) {

          if (t->type != JSMN_ARRAY && t->type != JSMN_OBJECT) {
            return_validation_error(8);
          }

          object_size = t->size;
          state = IN_ARGUMENTS_ARRAY;

          /* Handle empty arrays */
          jsmntok_t *tt = token_lookahead(i, 2);

          if (!tt || object_size <= 0) {
            state = SUCCESS;
            break;
          }

        } else {

          /* Skip unknown key/value pair */
          object_size -= 2;
        }

        /* ...steps in pairs */
        i++;
        break;
      }

      case IN_ARGUMENTS_ARRAY: {

        if (t->type != JSMN_PRIMITIVE && t->type != JSMN_STRING) {
          return_validation_error(9);
        }

        char *s = jsmn_stringify_token(p->json, t);

        /* Require that primitives are numeric */
        if (t->type == JSMN_PRIMITIVE && (!s || !isdigit(s[0]))) {
          return_validation_error(10);
        }

        rv[++n] = s;

        if (--object_size <= 0) {
          state = SUCCESS;
        }

        break;
      }

      case SUCCESS: {
        goto successful;
      }

      default: {
        return_validation_error(12);
        break;
      }
    }
  }

  if (state != SUCCESS) {
    return_validation_error(11);
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

/** --- **/

#define json_parser_tokens_start     (32)
#define json_parser_tokens_maximum   (32768)

/**
 * @name print_parsed_json:
 */
void print_parsed_json(parsed_json_t *p) {

  fprintf(stderr, "start\n");

  for (unsigned int i = 0; i < p->nr_tokens; ++i) {

    jsmntok_t *t = &p->tokens[i];
    char *s = jsmn_stringify_token(p->json, t);

    if (t->start == -1 || t->end == -1) {
      fprintf(stderr, "end\n");
      break;
    }

    switch (t->type) {
      case JSMN_STRING:
        fprintf(stderr, "string: '%s'\n", s);
        break;
      case JSMN_PRIMITIVE:
        fprintf(stderr, "primitive: '%s'\n", s);
        break;
      case JSMN_OBJECT:
        fprintf(stderr, "object[%d]\n", t->size);
        break;
      case JSMN_ARRAY:
        fprintf(stderr, "array[%d]\n", t->size);
        break;
      default:
        fprintf(stderr, "unknown-%d[%d]: '%s'\n", t->type, t->size, s);
        continue;
    }
  }
}

/**
 * @name parse_json:
 */
parsed_json_t *parse_json(char *json) {

  parsed_json_t *rv =
    (parsed_json_t *) malloc_and_zero(sizeof(parsed_json_t));

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

    /* Paranoia: check for overflow */
    if (multiplication_will_overflow(n, sizeof(jsmntok_t))) {
      return NULL;
    }

    jsmn_init(&rv->parser);
    rv->tokens = realloc(rv->tokens, n * sizeof(jsmntok_t));

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
    print_parsed_json(rv);

    char **argv = NULL;
    int argc = 0, err = 0;

    boolean_t r = parsed_json_to_arguments(rv, &argc, &argv, &err);

    printf("result: %s\n", (r ? "true" : "false"));
    printf("error status: %s\n", json_validation_error_to_string(err));

    printf("yield: ");

    for (int i = 0; i < argc; ++i) {
      printf("'%s' ", argv[i]);
    }

    printf("\n");
    free(argv);

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

/** --- **/

/**
 * @name initialize_transmit_status:
 */
transmit_status_t *initialize_transmit_status(transmit_status_t *t) {

  t->err = NULL;
  t->parts_sent = 0;
  t->parts_total = 0;
  t->finished = FALSE;

  t->message_index = 0;
  t->message_part_index = 0;

  for (unsigned int i = 0; i < GSM_MAX_MULTI_SMS; i++) {
    t->parts[i].err = NULL;
    t->parts[i].status = 0;
    t->parts[i].reference = 0;
    t->parts[i].transmitted = FALSE;
  }

  return t;
}

/**
 * @name initialize_delete_status:
 */
delete_status_t *initialize_delete_status(delete_status_t *d) {

  d->bitfield = NULL;
  d->requested = 0;

  d->examined = 0;
  d->skipped = 0;
  d->attempted = 0;
  d->errors = 0;
  d->deleted = 0;

  return d;
}

/**
 * @name initialize_application_options:
 */
app_options_t *initialize_application_options(app_options_t *o) {

  o->help = FALSE;
  o->repl = FALSE;
  o->invalid = FALSE;
  o->verbose = FALSE;
  o->application_name = NULL;
  o->gammu_configuration_path = NULL;

  return o;
}

/**
 * @name usage:
 */
static int usage() {

  fprintf(stderr, usage_text, app.application_name);
  return 127;
}

/**
 * @name print_usage_error:
 */
static void print_usage_error(const char *s) {

  fprintf(stderr, "Error: %s.\n", s);
  fprintf(stderr, "Use `-h' or `--help' to view usage information.\n");
}

/**
 * @name print_operation_error:
 */
static void print_operation_error(const char *s) {

  fprintf(stderr, "Error: %s.\n", s);
  fprintf(stderr, "Please check your command and try again.\n");
  fprintf(stderr, "Check Gammu's configuration if difficulties persist.\n");
}
/**
 * @name utf8_string_length:
 */
utf8_length_info_t *utf8_string_length(const char *str,
                                       utf8_length_info_t *i) {
  const char *p = str;
  unsigned int bytes = 0, symbols = 0;

  while (*p++) {
    if ((*p & 0xc0) != 0x80) {
      symbols++;
    }
    bytes++;
  }

  i->bytes = bytes;
  i->symbols = symbols;

  return i;
}

/** --- **/

/**
 * @name bitfield_create:
 */
bitfield_t *bitfield_create(unsigned int bits) {

  unsigned int size = bits + 1; /* One-based */
  unsigned int cells = (size / BITFIELD_CELL_WIDTH);

  bitfield_t *rv = (bitfield_t *) malloc_and_zero(sizeof(*rv));

  if (size % BITFIELD_CELL_WIDTH) {
    cells++;
  }

  rv->n = bits;
  rv->total_set = 0;
  rv->data = (uint8_t *) calloc(cells, BITFIELD_CELL_WIDTH);

  return rv;
}

/**
 * @name bitfield_destroy:
 */
void bitfield_destroy(bitfield_t *bf) {

  free(bf->data);
  free(bf);
}

/**
 * @name bitfield_test:
 *   Return true if the (zero-based or one-based) bit `bit` is set.
 *   Returns true on success, false if `bit` is out of range for this
 *   bitfield. You may use either zero-based addressing or one-based
 *   addressing, as long as you remain consistent for each instance
 *   of a bitfield_t.
 */
boolean_t bitfield_test(bitfield_t *bf, unsigned long bit) {

  if (bit > bf->n) {
    return FALSE;
  }

  unsigned long cell = (bit / BITFIELD_CELL_WIDTH);
  unsigned long offset = (bit % BITFIELD_CELL_WIDTH);

  return (bf->data[cell] & (1 << offset));
}

/**
 * @name bitfield_set:
 *   Set the (zero-based or one-based) bit `bit` to one if `value` is
 *   true, otherwise set the bit to zero. Returns true on success, false
 *   if the bit `bit` is out of range for this particular bitfield.  You
 *   may use either zero-based addressing or one-based addressing, as
 *   long as you remain consistent for each instance of a bitfield_t.
 */
boolean_t bitfield_set(bitfield_t *bf, unsigned long bit, boolean_t
        value) {

  if (bit > bf->n) {
    return FALSE;
  }

  unsigned long cell = (bit / BITFIELD_CELL_WIDTH);
  unsigned long offset = (bit % BITFIELD_CELL_WIDTH);

  int prev_value = (
    (bf->data[cell] & (1 << offset)) != 0
  );

  if (value) {
    bf->data[cell] |= (1 << offset);
  } else {
    bf->data[cell] &= ~(1 << offset);
  }

  if (value && !prev_value) {
    bf->total_set++;
  } else if (prev_value && !value) {
    bf->total_set--;
  }

  return TRUE;
}

/**
 * @name find_maximum_integer_argument:
 */
boolean_t find_maximum_integer_argument(unsigned long *rv, char *argv[]) {

  unsigned long max = 0;
  boolean_t found = FALSE;

  for (unsigned int i = 0; argv[i] != NULL; i++) {

    char *err = NULL;
    unsigned long n = strtoul(argv[i], &err, 10);

    if (err == NULL || *err != '\0') {
      continue;
    }

    if (n > max) {
      max = n;
      found = TRUE;
    }
  }

  *rv = max;
  return found;
}

/**
 * @name bitfield_set_integer_arguments:
 */
boolean_t bitfield_set_integer_arguments(bitfield_t *bf, char *argv[]) {

  for (unsigned int i = 0; argv[i] != NULL; i++) {

    char *err = NULL;
    unsigned long n = strtoul(argv[i], &err, 10);

    if (err == NULL || *err != '\0') {
      return FALSE;
    }

    if (!bitfield_set(bf, n, TRUE)) {
      return FALSE;
    }
  }

  return TRUE;
}

/** --- **/

/**
 * @name ucs2_encode_json_utf8:
 *   Copy and transform the string `s` to a newly-allocated
 *   buffer, making it suitable for output as a single utf-8
 *   JSON string. The caller must free the returned string.
 */
char *ucs2_encode_json_utf8(const unsigned char *s) {

  unsigned int i, j = 0;
  int ul = UnicodeLength(s);

  /* Worst-case UCS-2 string allocation:
   *  Original length plus null terminator; two bytes for each
   *  character; every character escaped with a UCS-2 backslash. */

  unsigned char *b =
    (unsigned char *) malloc_and_zero((ul + 1) * 2 * 2);

  for (i = 0; i < ul; ++i) {
    unsigned char msb = s[2 * i], lsb = s[2 * i + 1];

    if (msb == '\0') {
      unsigned char escape = '\0';

      switch (lsb) {
        case '\r':
          escape = 'r'; break;
        case '\n':
          escape = 'n'; break;
        case '\f':
          escape = 'f'; break;
        case '\b':
          escape = 'b'; break;
        case '\t':
          escape = 't'; break;
        case '\\': case '"':
          escape = lsb; break;
        default:
          break;
      };

      if (escape != '\0') {
        b[j++] = '\0';
        b[j++] = '\\';
        lsb = escape;
      }
    }

    b[j++] = msb;
    b[j++] = lsb;
  }

  b[j++] = '\0';
  b[j++] = '\0';

  /* Worst-case UTF-8:
   *  Four bytes per character (see RFC3629); 1-byte null terminator. */

  char *rv = (char *) malloc_and_zero(4 * UnicodeLength(b) + 1);

  EncodeUTF8(rv, b);
  free(b);

  return rv;
}

/**
 * @name encode_timestamp_utf8:
 */
char *encode_timestamp_utf8(message_timestamp_t *t) {

  int n = TIMESTAMP_MAX_WIDTH;
  char *rv = (char *) malloc_and_zero(n);

  #ifdef _WIN32
    #pragma warning(disable: 4996)
  #endif

  snprintf(
    rv, n, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
      t->Year, t->Month, t->Day, t->Hour, t->Minute, t->Second
  );

  return rv;
}

/**
 * @name ucs2_is_gsm_codepoint:
 *   Given the most-significant byte `msb` and the least-significant
 *   byte `lsb` of a UCS-2 character, return TRUE if the character
 *   can be represented in the default GSM alphabet (described in GSM
 *   03.38). The GSM-to-Unicode conversion table used here was obtained
 *   from http://www.unicode.org/Public/MAPPINGS/ETSI/GSM0338.TXT.
 *
 *   Copyright (c) 2000 - 2009 Unicode, Inc. All Rights reserved.
 *   Unicode, Inc. hereby grants the right to freely use the information
 *   supplied in this file in the creation of products supporting the
 *   Unicode Standard, and to make copies of this file in any form for
 *   internal or external distribution as long as this notice remains
 *   attached.
 *  
 */
boolean_t ucs2_is_gsm_codepoint(uint8_t msb, uint8_t lsb) {

  switch (msb) {

    case 0x00: {
      int rv = (
        (lsb >= 0x20 && lsb <= 0x5f)
          || (lsb >= 0x61 && lsb <= 0x7e)
          || (lsb >= 0xa3 && lsb <= 0xa5)
          || (lsb >= 0xc4 && lsb <= 0xc6)
          || (lsb >= 0xe4 && lsb <= 0xe9)
      );
      if (rv) {
        return TRUE;
      }
      switch (lsb) {
        case 0x0a: case 0x0c: case 0x0d:
        case 0xa0: case 0xa1: case 0xa7:
        case 0xbf: case 0xc9: case 0xd1:
        case 0xd6: case 0xd8: case 0xdc:
        case 0xdf: case 0xe0: case 0xec:
        case 0xf1: case 0xf2: case 0xf6:
        case 0xf8: case 0xf9: case 0xfc:
          return TRUE;
        default:
          return FALSE;
      }
    }
    case 0x03: {
      switch (lsb) {
        case 0x93: case 0x94:
        case 0x98: case 0x9b:
        case 0x9e: case 0xa0:
        case 0xa3: case 0xa6:
        case 0xa8: case 0xa9:
          return TRUE;
        default:
          return FALSE;
      }
    }
    case 0x20: {
      return (lsb == 0xac);
    }
    default:
      break;
  }

  return FALSE;
}

/**
 * @name ucs2_is_gsm_string:
 *   Return true if the UCS-2 string `s` can be represented in
 *   the GSM default alphabet. The input string should be terminated
 *   by the UCS-2 null character (i.e. two null bytes).
 */
boolean_t ucs2_is_gsm_string(const unsigned char *s) {

  int ul = UnicodeLength(s);

  for (unsigned int i = 0; i < ul; ++i) {
    if (!ucs2_is_gsm_codepoint(s[2 * i], s[2 * i + 1])) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * @name is_empty_timestamp:
 */
boolean_t is_empty_timestamp(message_timestamp_t *t) {

  return (
    t->Year == 0 && t->Month == 0 && t->Day == 0 &&
      t->Hour == 0 && t->Minute == 0 && t->Second == 0
  );
}

/** --- **/

/**
 * @name gammu_create:
 */
gammu_state_t *gammu_create(const char *config_path) {

  gammu_state_t *s =
    (gammu_state_t *) malloc_and_zero(sizeof(*s));

  if (!s) {
    return NULL;
  }

  INI_Section *ini;
  GSM_InitLocales(NULL);

  s->err = ERR_NONE;
  s->sm = GSM_AllocStateMachine();

  if ((s->err = GSM_FindGammuRC(&ini, config_path)) != ERR_NONE) {
    goto failure;
  }

  GSM_Config *cfg = GSM_GetConfig(s->sm, 0);

  if ((s->err = GSM_ReadConfig(ini, cfg, 0)) != ERR_NONE) {
    goto failure;
  }

  INI_Free(ini);
  GSM_SetConfigNum(s->sm, 1);

  if ((s->err = GSM_InitConnection(s->sm, 1)) != ERR_NONE) {
    goto failure;
  }

  /* Success */
  return s;

  failure:
    free(s);
    return NULL;
}

/**
 * @name gammu_create_if_necessary:
 */
gammu_state_t *gammu_create_if_necessary(gammu_state_t **sp) {

  if (*sp != NULL) {
    return *sp;
  }

  gammu_state_t *rv = gammu_create(app.gammu_configuration_path);

  if (!rv) {
    return NULL;
  }

  if (app.verbose) {

    /* Enable global debugging to stderr */
    GSM_Debug_Info *debug_info = GSM_GetGlobalDebug();
    GSM_SetDebugFileDescriptor(stderr, TRUE, debug_info);
    GSM_SetDebugLevel("textall", debug_info);

    /* Enable state machine debugging to stderr */
    debug_info = GSM_GetDebug(rv->sm);
    GSM_SetDebugGlobal(FALSE, debug_info);
    GSM_SetDebugFileDescriptor(stderr, TRUE, debug_info);
    GSM_SetDebugLevel("textall", debug_info);
  }

  *sp = rv;
  return rv;
}

/**
 * @name gammu_destroy:
 */
void gammu_destroy(gammu_state_t *s) {

  GSM_TerminateConnection(s->sm);
  GSM_FreeStateMachine(s->sm);

  free(s);
}

/** --- **/

/**
 * @name for_each_message:
 */
boolean_t for_each_message(gammu_state_t *s,
                           message_iterate_fn_t fn, void *x) {
  boolean_t rv = FALSE;
  boolean_t start = TRUE;

  multimessage_t *sms =
    (multimessage_t *) malloc_and_zero(sizeof(*sms));

  for (;;) {

    int err = GSM_GetNextSMS(s->sm, sms, start);

    if (err == ERR_EMPTY) {
      rv = TRUE;
      break;
    }

    if (err != ERR_NONE) {
      break;
    }

    rv = TRUE;

    if (!fn(s, sms, start, x)) {
      break;
    }

    start = FALSE;
  }

  free(sms);
  return rv;
}

/**
 * @name print_message_json_utf8:
 */
boolean_t print_message_json_utf8(gammu_state_t *s,
                                  multimessage_t *sms,
                                  int is_start, void *x) {
  if (!is_start) {
    printf(", ");
  }

  for (unsigned int i = 0; i < sms->Number; i++) {

    printf("{");

    /* Modem file/location information */
    printf("\"folder\": %d, ", sms->SMS[i].Folder);
    printf("\"location\": %d, ", sms->SMS[i].Location);

    /* Originating phone number */
    char *from = ucs2_encode_json_utf8(sms->SMS[i].Number);
    printf("\"from\": \"%s\", ", from);
    free(from);

    /* Phone number of telco's SMS service center */
    char *smsc = ucs2_encode_json_utf8(sms->SMS[i].SMSC.Number);
    printf("\"smsc\": \"%s\", ", smsc);
    free(smsc);

    /* Receive timestamp */
    if (is_empty_timestamp(&sms->SMS[i].DateTime)) {
      printf("\"timestamp\": false, ");
    } else {
      char *timestamp = encode_timestamp_utf8(&sms->SMS[i].DateTime);
      printf("\"timestamp\": \"%s\", ", timestamp);
      free(timestamp);
    }

    /* SMSC receive timestamp */
    if (is_empty_timestamp(&sms->SMS[i].SMSCTime)) {
      printf("\"smsc_timestamp\": false, ");
    } else {
      char *smsc_timestamp = encode_timestamp_utf8(&sms->SMS[i].SMSCTime);
      printf("\"smsc_timestamp\": \"%s\", ", smsc_timestamp);
      free(smsc_timestamp);
    }

    /* Multi-part message metadata */
    int parts = sms->SMS[i].UDH.AllParts;
    int part = sms->SMS[i].UDH.PartNumber;

    printf("\"segment\": %d, ", (part > 0 ? part : 1));
    printf("\"total_segments\": %d, ", (parts > 0 ? parts : 1));

    /* Identifier from user data header */
    if (sms->SMS[i].UDH.Type == UDH_NoUDH) {
      printf("\"udh\": false, ");
    } else {
      if (sms->SMS[i].UDH.ID16bit != -1) {
        printf("\"udh\": %d, ", sms->SMS[i].UDH.ID16bit);
      } else if (sms->SMS[i].UDH.ID8bit != -1) {
        printf("\"udh\": %d, ", sms->SMS[i].UDH.ID8bit);
      } else {
        printf("\"udh\": null, ");
      }
    }

    /* Text and text encoding */
    switch (sms->SMS[i].Coding) {
      case SMS_Coding_8bit: {
        printf("\"encoding\": \"binary\", ");
        break;
      }
      case SMS_Coding_Default_No_Compression:
      case SMS_Coding_Unicode_No_Compression: {
        printf("\"encoding\": \"utf-8\", ");
        char *text = ucs2_encode_json_utf8(sms->SMS[i].Text);
        printf("\"content\": \"%s\", ", text);
        free(text);
        break;
      }
      case SMS_Coding_Unicode_Compression:
      case SMS_Coding_Default_Compression: {
        printf("\"encoding\": \"unsupported\", ");
        break;
      }
      default: {
        printf("\"encoding\": \"invalid\", ");
        break;
      }
    }

    printf("\"inbox\": %s", sms->SMS[i].InboxFolder ? "true" : "false");
    printf("}");
  }

  fflush(stdout);
  return TRUE;
}

/**
 * @name print_messages_json_utf8:
 */
int print_messages_json_utf8(gammu_state_t *s) {

  printf("[");

  boolean_t rv = for_each_message(
    s, (message_iterate_fn_t) print_message_json_utf8, NULL
  );

  printf("]\n");
  return rv;
}

/**
 * @name action_retrieve_messages:
 */
int action_retrieve_messages(gammu_state_t **sp, int argc, char *argv[]) {

  int rv = 0;
  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    print_operation_error("failed to start gammu");
    rv = 1; goto cleanup;
  }

  if (!print_messages_json_utf8(s)) {
    print_operation_error("failed to retrieve one or more messages");
    rv = 2; goto cleanup;
  }

  cleanup:
    return rv;
}

/** --- **/

/**
 * @name print_deletion_detail_json_utf8:
 */
void print_deletion_detail_json_utf8(message_t *sms,
                                     delete_stage_t r,
                                     boolean_t is_start) {
  if (!is_start) {
    printf(", ");
  }

  printf("\"%d\": ", sms->Location);

  switch (r) {
    case DELETE_SKIPPED:
      printf("\"skip\"");
      break;
    case DELETE_SUCCESS:
      printf("\"ok\"");
      break;
    default:
    case DELETE_ERROR:
      printf("\"error\"");
      break;
  }

  fflush(stdout);
}

/**
 * @name print_deletion_status_json_utf8:
 */
void print_deletion_status_json_utf8(delete_status_t *status) {

  printf("\"totals\": {");

  if (status->requested > 0) {
    printf("\"requested\": %d, ", status->requested);
  } else {
    printf("\"requested\": \"all\", ");
  }

  printf("\"examined\": %d, ", status->examined);
  printf("\"attempted\": %d, ", status->attempted);
  printf("\"skipped\": %d, ", status->skipped);
  printf("\"errors\": %d, ", status->errors);
  printf("\"deleted\": %d", status->deleted);

  printf("}, ");

  if (status->deleted == 0) {
    printf("\"result\": \"none\"");
    return;
  }

  unsigned int total = (
    (status->requested == 0) ?
      status->examined : status->requested
  );

  if (status->deleted < total) {
    printf("\"result\": \"partial\"");
  } else if (status->deleted == total) {
    printf("\"result\": \"success\"");
  } else {
    printf("\"result\": \"internal-error\"");
  }
}

/**
 * @name add_deletion_result_to_status:
 */
void add_deletion_result_to_status(delete_stage_t result,
                                   delete_status_t *status) {

  switch (result) {
    case DELETE_EXAMINING:
      status->examined++;
      break;
    case DELETE_ATTEMPTING:
      status->attempted++;
      break;
    case DELETE_SKIPPED:
      status->skipped++;
      break;
    case DELETE_ERROR:
      status->errors++;
      break;
    case DELETE_SUCCESS:
      status->deleted++;
      break;
    default:
    case DELETE_RESULT_BARRIER:
      fprintf(stderr, "Unhandled deletion result '%d'\n", result);
      break;
  }
}

/**
 * @name delete_multimessage:
 */
boolean_t delete_multimessage(gammu_state_t *s,
                              multimessage_t *sms,
                              bitfield_t *bitfield,
                              delete_callback_fn_t callback, void *x) {
  int rv = TRUE;

  for (unsigned int i = 0; i < sms->Number; i++) {

    message_t *m = &sms->SMS[i];

    if (callback) {
      callback(s, m, DELETE_EXAMINING, x);
    }

    if (bitfield && !bitfield_test(bitfield, m->Location)) {
      if (callback) {
        callback(s, m, DELETE_SKIPPED, x);
      }
      continue;
    }

    if (callback) {
      callback(s, m, DELETE_ATTEMPTING, x);
    }

    if ((s->err = GSM_DeleteSMS(s->sm, m)) != ERR_NONE) {
      if (callback) {
        callback(s, m, DELETE_ERROR, x);
      }
      rv = FALSE;
      continue;
    }

    if (callback) {
      callback(s, m, DELETE_SUCCESS, x);
    }
  }

  return rv;
}

/**
 * @name _after_deletion_callback:
 */
static void _after_deletion_callback(gammu_state_t *s, message_t *sms,
                                     delete_stage_t r, void *x) {

  delete_status_t *status = (delete_status_t *) x;

  /* Update totals */
  add_deletion_result_to_status(r, status);

  /* JSON per-item output */
  if (r > DELETE_RESULT_BARRIER) {
    print_deletion_detail_json_utf8(sms, r, status->is_start);
  }
};

/**
 * @name _before_deletion_callback:
 */
static boolean_t _before_deletion_callback(gammu_state_t *s,
                                           multimessage_t *sms,
                                           boolean_t is_start, void *x) {

  delete_status_t *status = (delete_status_t *) x;

  status->is_start = is_start;

  if (status->bitfield) {
    status->requested = status->bitfield->total_set;
  }

  return delete_multimessage(
    s, sms, status->bitfield, _after_deletion_callback, x
  );
};

/**
 * @name delete_selected_messages:
 */
boolean_t delete_selected_messages(gammu_state_t *s, bitfield_t *bf) {

  delete_status_t status;

  initialize_delete_status(&status);
  status.bitfield = bf;

  printf("\"detail\": {");

  boolean_t rv = for_each_message(
    s, _before_deletion_callback, (void *) &status
  );

  printf("}, ");

  /* JSON summary output */
  print_deletion_status_json_utf8(&status);
  return rv;
}

/**
 * @name action_delete_messages:
 */
int action_delete_messages(gammu_state_t **sp, int argc, char *argv[]) {

  int rv = 0;
  bitfield_t *bf = NULL;

  if (argc < 2) {
    print_usage_error("deletion location(s) must be specified");
    return 1;
  }

  int delete_all = (strcmp(argv[1], "all") == 0);

  if (!delete_all) {

    unsigned long n;
    boolean_t found = find_maximum_integer_argument(&n, &argv[1]);

    if (!found) {
      print_usage_error("no valid location(s) specified");
      rv = 2; goto cleanup;
    }

    if (n == ULONG_MAX && errno == ERANGE) {
      print_usage_error("integer argument would overflow");
      rv = 3; goto cleanup;
    }

    bf = bitfield_create(n);

    if (!bf) {
      print_operation_error("failed to create deletion index");
      rv = 4; goto cleanup_delete;
    }

    if (!bitfield_set_integer_arguments(bf, &argv[1])) {
      print_operation_error("one or more location(s) are invalid");
      rv = 5; goto cleanup_delete;
    }
  }

  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    print_operation_error("failed to initialize gammu");
    rv = 6; goto cleanup_delete;
  }

  printf("{");

  if (!delete_selected_messages(s, bf)) {
    print_operation_error("failed to delete one or more messages");
    rv = 7; goto cleanup_json;
  }

  cleanup_json:
    printf("}\n");

  cleanup_delete:
    if (bf) {
      bitfield_destroy(bf);
    }

  cleanup:
    return rv;
}

/** --- **/

/**
 * @name print_json_transmit_status:
 */
void print_json_transmit_status(gammu_state_t *s, multimessage_t *m,
                                transmit_status_t *t, boolean_t is_start) {

  if (!is_start) {
    printf(", ");
  }

  printf("{");
  printf("\"index\": %d, ", t->message_index);

  if (t->err != NULL) {

    printf("\"result\": \"error\", ");
    printf("\"error\": \"%s\"", t->err); /* const */

  } else {

    /* Result */
    if (t->parts_sent <= 0) {
      printf("\"result\": \"error\", ");
    } else if (t->parts_sent < t->parts_total) {
      printf("\"result\": \"partial\", ");
    } else {
      printf("\"result\": \"success\", ");
    }

    /* Multi-part message information */
    printf("\"parts_sent\": %d, ", t->parts_sent);
    printf("\"parts_total\": %d, ", t->parts_total);

    /* Per-part status codes */
    printf("\"parts\": [");

    /* Per-part information */
    for (unsigned int i = 0; i < t->parts_total; i++) {

      if (i != 0) {
        printf(", ");
      }

      printf("{");

      if (t->parts[i].err) {
        printf("\"result\": \"error\", ");
        printf("\"error\": \"%s\", ", t->parts[i].err); /* const */
      } else {
        printf("\"result\": \"success\", ");
        char *text = ucs2_encode_json_utf8(m->SMS[i].Text);
        printf("\"content\": \"%s\", ", text);
        free(text);
      }

      printf("\"index\": %d, ", i + 1);
      printf("\"status\": %d, ", t->parts[i].status);
      printf("\"reference\": %d", t->parts[i].reference);

      printf("}");
    }

    printf("]");
  }

  printf("}");
  fflush(stdout);
}

/**
 * @name _message_transmit_callback:
 */
static void _message_transmit_callback(GSM_StateMachine *sm,
                                       int status, int ref, void *x) {

  transmit_status_t *t = (transmit_status_t *) x;
  unsigned int i = t->message_part_index;

  if (status == 0) {
    t->parts[i].transmitted = TRUE;
  }

  t->finished = TRUE;
  t->parts[i].status = status;
  t->parts[i].reference = ref;
}

/**
 * @name action_send_messages:
 */
int action_send_messages(gammu_state_t **sp, int argc, char *argv[]) {

  int rv = 0;
  char **argp = &argv[1];

  if (argc <= 2) {
    print_usage_error("not enough arguments provided");
    return 1;
  }

  if (argc % 2 != 1) {
    print_usage_error("odd number of arguments provided");
    return 2;
  }

  /* Allocate */
  smsc_t *smsc =
    (smsc_t *) malloc_and_zero(sizeof(*smsc));

  multimessage_t *sms =
    (multimessage_t *) malloc_and_zero(sizeof(*sms));

  multimessage_info_t *info =
    (multimessage_info_t *) malloc_and_zero(sizeof(*info));

  /* Lazy initialization of libgammu */
  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    print_operation_error("failed to start gammu subsystem");
    rv = 3; goto cleanup;
  }

  /* Find SMSC number */
  smsc->Location = 1;

  if ((s->err = GSM_GetSMSC(s->sm, smsc)) != ERR_NONE) {
    print_operation_error("failed to discover SMSC phone number");
    rv = 4; goto cleanup_sms;
  }

  transmit_status_t status;
  initialize_transmit_status(&status);

  GSM_SetSendSMSStatusCallback(
    s->sm, _message_transmit_callback, &status
  );

  boolean_t is_start = TRUE;
  unsigned int message_index = 0;

  printf("[");

  /* For each message... */
  while (*argp != NULL) {

    GSM_ClearMultiPartSMSInfo(info);
    GSM_Debug_Info *debug = GSM_GetGlobalDebug();

    /* Destination phone number */
    utf8_length_info_t nl;
    char *sms_destination_number = *argp++;
    utf8_string_length(sms_destination_number, &nl);

    /* Check size of phone number:
        We'll be decoding this in to a fixed-sized buffer. */

    if (nl.symbols > GSM_MAX_NUMBER_LENGTH) {
      status.err = "Phone number is too long";
      goto cleanup_transmit_status;
    }

    /* Missing message text:
        This shouldn't happen since we check `argc` above,
        but I'm leaving this here in case we refactor later. */

    if (*argp == NULL) {
      status.err = "No message body provided";
      goto cleanup_transmit_status;
    }

    /* UTF-8 message content */
    utf8_length_info_t ml;
    char *sms_message = *argp++;
    utf8_string_length(sms_message, &ml);

    /* Convert message from UTF-8 to UCS-2:
        Every symbol is two bytes long; the string is then
        terminated by a single 2-byte UCS-2 null character. */

    unsigned char *sms_message_ucs2 =
      (unsigned char *) malloc_and_zero((ml.symbols + 1) * 2);

    DecodeUTF8(sms_message_ucs2, sms_message, ml.bytes);

    /* Prepare message info structure:
        This information is used to encode the possibly-multipart SMS. */

    info->Class = 1;
    info->EntriesNum = 1;
    info->Entries[0].Buffer = sms_message_ucs2;
    info->Entries[0].ID = SMS_ConcatenatedTextLong;
    info->UnicodeCoding = !ucs2_is_gsm_string(sms_message_ucs2);

    if ((s->err = GSM_EncodeMultiPartSMS(debug, info, sms)) != ERR_NONE) {
      status.err = "Failed to encode message";
      goto cleanup_sms_text;
    }

    status.parts_sent = 0;
    status.parts_total = sms->Number;

    /* For each SMS part... */
    for (unsigned int i = 0; i < sms->Number; i++) {

      status.finished = FALSE;
      status.message_part_index = i;

      sms->SMS[i].PDU = SMS_Submit;

      /* Copy destination phone number:
           This is a fixed-size buffer; size was already checked above. */

      CopyUnicodeString(sms->SMS[i].SMSC.Number, smsc->Number);
      DecodeUTF8(sms->SMS[i].Number, sms_destination_number, nl.bytes);

      /* Transmit a single message part */
      if ((s->err = GSM_SendSMS(s->sm, &sms->SMS[i])) != ERR_NONE) {
        status.parts[i].err = "Message transmission failed";
        continue;
      }

      for (;;) {
        /* Wait for reply */
        GSM_ReadDevice(s->sm, TRUE);

        if (status.finished) {
          break;
        }
      }

      if (!status.parts[i].transmitted) {
        status.parts[i].err = "Message delivery failed";
        continue;
      }

      status.parts_sent++;
    }

    cleanup_sms_text:
      status.message_index = ++message_index;
      free(sms_message_ucs2);

    cleanup_transmit_status:
      print_json_transmit_status(s, sms, &status, is_start);
      is_start = FALSE;
  }

  cleanup_sms:
    free(sms);
    free(smsc);
    free(info);
    printf("]\n");
  
  cleanup:
    return rv;
}

/** --- **/

/**
 * @name parse_global_arguments:
 */
int parse_global_arguments(int argc, char *argv[], app_options_t *o) {

  int rv = 0;
  char **argp = argv;

  while (*argp != NULL) {

    if (strcmp(*argp, "-h") == 0 || strcmp(*argp, "--help") == 0) {
      o->help = TRUE;
      break;
    }

    if (strcmp(*argp, "-c") == 0 || strcmp(*argp, "--config") == 0) {

      if (*++argp == NULL) {
        print_usage_error("no configuration file name provided");
        o->invalid = TRUE;
        break;
      }

      o->gammu_configuration_path = *argp++;
      rv += 2;

      continue;
    }

    if (strcmp(*argp, "-v") == 0 || strcmp(*argp, "--verbose") == 0) {
      o->verbose = TRUE;
      ++argp; ++rv;
      continue;
    }

    if (strcmp(*argp, "-r") == 0 || strcmp(*argp, "--repl") == 0) {
      o->repl = TRUE;
      ++argp; ++rv;
      continue;
    }

    break;
  }

  return rv;
}

/**
 * @name process_command:
 *   Execute a command, based upon the arguments provided.
 *   The `argv[0]` argument should contain a single command
 *   (currently either `send`, `retrieve`, or `delete`); the
 *   remaining items in `argv` are parameters to be provided to
 *   the specified command. Return `true` if a command was
 *   executed (whether successfully or resulting in an error),
 *   or `false` if the command specified was not found.
 */
boolean_t process_command(gammu_state_t *s,
                          int argc, char *argv[], int *rv) {

  /* Option #1:
   *   Retrieve all messages as a JSON array. */

  if (argc > 0 && strcmp(argv[0], "retrieve") == 0) {
    *rv = action_retrieve_messages(&s, argc, argv);
    return TRUE;
  }

  /* Option #2:
   *   Delete messages specified in `argv` (or all messages). */

  if (argc > 0 && strcmp(argv[0], "delete") == 0) {
    *rv = action_delete_messages(&s, argc, argv);
    return TRUE;
  }

  /* Option #3:
   *   Send one or more messages, each to a single recipient. */

  if (argc > 0 && strcmp(argv[0], "send") == 0) {
    *rv = action_send_messages(&s, argc, argv);
    return TRUE;
  }

  return FALSE;
}

/**
 * @name process_repl_commands:
 */
boolean_t process_repl_commands(FILE *stream) {

  for (;;) {

    size_t n = 0;
    char *line = NULL;
    ssize_t rv = getline(&line, &n, stream);

    if (rv < 0) {
      break;
    }

    parsed_json_t *p = parse_json(line);

    if (!p) {

      printf("{");
      printf("\"result\": \"error\",");
      printf("\"error\": \"Parse error\", \"errno\": 1");
      printf("}\n");

      goto parse_error;
    }

    release_parsed_json(p);

    parse_error:
      free(line);
  }

  return TRUE;
}

/**
 * @name main:
 */
int main(int argc, char *argv[]) {

  int rv = 0;
  char **argp = argv;

  gammu_state_t *s = NULL;
  initialize_application_options(&app);

  argc -= 1;
  app.application_name = *argp++;

  int n = parse_global_arguments(argc, argp, &app);

  if (app.invalid) {
    print_usage_error("one or more invalid argument(s) provided");
    goto cleanup;
  }
  
  if (app.help) {
    usage();
    goto cleanup;
  }

  argc -= n;
  argp += n;

  /* Execute command:
   *   This runs the operation provided via command-line arguments. */

  if (argc > 0) {
    if (!process_command(s, argc, argp, &rv)) {
      print_usage_error("invalid command specified");
      goto cleanup;
    }
  } else if (!app.repl) {
    print_usage_error("no command specified");
    goto cleanup;
  }

  /* Read, execute, print loop:
   *  Repeatedly read lines of JSON from standard input, parse
   *  them in to command/arguments tuples, dispatch these tuples
   *  to `process_command`, and repeat until reaching end-of-file. */

  if (app.repl && rv == 0) {
    process_repl_commands(stdin);
  }

  cleanup:
    if (s) {
      gammu_destroy(s);
    }
    return rv;
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */

