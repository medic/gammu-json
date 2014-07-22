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

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <inttypes.h>

#include <iconv.h>
#include <gammu.h>
#include <jsmn.h>

#include "json.h"
#include "allocate.h"
#include "bitfield.h"
#include "encoding.h"
#include "gammu-json.h"

/** --- **/

#define timestamp_max_width     (64)
#define read_line_size_start    (1024)
#define read_line_size_maximum  (4194304)

/** --- **/

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
  "  Copyright (c) 2013-2014 David Brown <hello at scri.pt>.\n"
  "  Copyright (c) 2013-2014 Medic Mobile, Inc. <david at medicmobile.org>\n"
  "\n"
  "  Released under the GNU General Public License, version three.\n"
  "  For more information, see <http://github.com/browndav/gammu-json>.\n"
  "\n"
);
/** --- **/

/**
 * @name operation_errors:
 */
static const char *const operation_errors[] = {
  /* 0 */  "success; no error",
  /* 1 */  "failed to initialize gammu",
  /* 2 */  "failed to discover SMSC phone number",
  /* 3 */  "failed to retrieve one or more messages",
  /* 4 */  "one or more SMS locations are invalid",
  /* 5 */  "failed to create in-memory message index",
  /* 6 */  "failed to delete one or more messages",
  /* 7 */  "parse error while processing JSON input"
};

/**
 * @name usage_errors:
 */
static const char *const usage_errors[] = {
  /* 0 */  "success; no error",
  /* 1 */  "not enough arguments provided",
  /* 2 */  "odd number of arguments provided",
  /* 3 */  "no configuration file name provided",
  /* 4 */  "one or more invalid argument(s) provided",
  /* 5 */  "invalid command specified",
  /* 6 */  "no command specified",
  /* 7 */  "location(s) must be specified",
  /* 8 */  "no valid location(s) specified",
  /* 9 */  "integer argument would overflow"
};

/** --- **/

static app_options_t app; /* global */

/** --- **/

/**
 * @name read_line:
 *   Read a line portably, relying only upon the C library's
 *   `getc` standard I/O function. This should work on any
 *   platform that has a standard I/O (stdio) implementation.
 */
char *read_line(FILE *stream, boolean_t *eof) {

  int c;
  unsigned int i = 0;
  size_t size = read_line_size_start;

  char *rv = allocate_array(sizeof(char), size, 1);

  for (;;) {

    if (!size || i >= size) {
      if ((size *= 8) >= read_line_size_maximum) {
        goto allocation_error;
      }
      if (!(rv = reallocate_array(rv, sizeof(char), size, 1))) {
        goto allocation_error;
      }
    }

    c = getc(stream);

    if (c == '\n') {
      break;
    } else if (c == EOF) {
      *eof = TRUE;
      break;
    }

    rv[i++] = c;
  }

  rv[i] = '\0';
  return rv;

  allocation_error:

    if (rv) {
      free(rv);
    }

    return NULL;
}

/* --- */

/**
 * @name usage:
 */
static int usage() {

  fprintf(stderr, usage_text, app.application_name);
  return 127;
}

/**
 * @name print_repl_error:
 */
void print_repl_error(int err, const char *s) {

  printf("{ ");
  printf("\"result\": \"error\", ");
  printf("\"errno\": %d, \"error\": \"%s\"", err, s);
  printf(" }\n");
}

/**
 * @name print_usage_error:
 */
static void print_usage_error(usage_error_t err) {

  const char *s = (
    err < U_ERR_BARRIER ?
      usage_errors[err] : "Unknown or unhandled error"
  );

  if (app.repl) {
    print_repl_error(err, s);
  } else {
    fprintf(stderr, "Error: %s.\n", s);
    fprintf(stderr, "Use `-h' or `--help' to view usage information.\n");
  }
}

/**
 * @name print_operation_error:
 */
static void print_operation_error(operation_error_t err) {

  const char *s = (
    err < OP_ERR_BARRIER ?
      operation_errors[err] : "unknown or unhandled error"
  );

  if (app.repl) {
    print_repl_error(err, s);
  } else {
    fprintf(stderr, "Error: %s.\n", s);
    fprintf(stderr, "Please check your command and try again.\n");
    fprintf(stderr, "Check Gammu's configuration if problems persist.\n");
  }
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

    if (n >= max) {
      max = n;
      found = TRUE;
    }
  }

  *rv = max;
  return found;
}


/**
 * @name encode_timestamp_utf8:
 */
char *encode_timestamp_utf8(message_timestamp_t *t) {

  char *rv = allocate(timestamp_max_width);

  #ifdef _WIN32
    #pragma warning(disable: 4996)
  #endif

  snprintf(
    rv, timestamp_max_width, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
      t->Year, t->Month, t->Day, t->Hour, t->Minute, t->Second
  );

  return (char *) rv;
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

  gammu_state_t *s = allocate(sizeof(*s));

  INI_Section *ini;
  GSM_InitLocales(NULL);

  if ((s->err = GSM_FindGammuRC(&ini, config_path)) != ERR_NONE) {
    goto cleanup;
  }

  s->sm = GSM_AllocStateMachine();
  GSM_Config *cfg = GSM_GetConfig(s->sm, 0);

  if ((s->err = GSM_ReadConfig(ini, cfg, 0)) != ERR_NONE) {
    goto cleanup_state;
  }

  INI_Free(ini);
  GSM_SetConfigNum(s->sm, 1);

  if ((s->err = GSM_InitConnection(s->sm, 1)) != ERR_NONE) {
    goto cleanup_state;
  }

  /* Success */
  return s;

  cleanup_state:
    GSM_FreeStateMachine(s->sm);
  
  cleanup:
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

  multimessage_t *sms = allocate(sizeof(*sms));

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
                                  boolean_t is_start, void *x) {
  if (!is_start) {
    printf(", ");
  }

  for (unsigned int i = 0; i < sms->Number; i++) {

    printf("{ ");

    /* Modem file/location information */
    printf("\"folder\": %d, ", sms->SMS[i].Folder);
    printf("\"location\": %d, ", sms->SMS[i].Location);

    /* Originating phone number */
    char *from =
      utf16be_encode_json_utf8((char *) sms->SMS[i].Number);

    printf("\"from\": \"%s\", ", from);
    free(from);

    /* SMS service center phone number */
    char *smsc =
      utf16be_encode_json_utf8((char *) sms->SMS[i].SMSC.Number);

    printf("\"smsc\": \"%s\", ", smsc);
    free(smsc);

    /* Receive timestamp */
    if (is_empty_timestamp(&sms->SMS[i].DateTime)) {
      printf("\"timestamp\": false, ");
    } else {
      char *timestamp =
        encode_timestamp_utf8(&sms->SMS[i].DateTime);

      printf("\"timestamp\": \"%s\", ", timestamp);
      free(timestamp);
    }

    /* SMSC receive timestamp */
    if (is_empty_timestamp(&sms->SMS[i].SMSCTime)) {
      printf("\"smsc_timestamp\": false, ");
    } else {
      char *smsc_timestamp =
        encode_timestamp_utf8(&sms->SMS[i].SMSCTime);

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

        char *text =
          utf16be_encode_json_utf8((char *) sms->SMS[i].Text);

        printf("\"encoding\": \"utf-8\", ");
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
    printf(" }");
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
int action_retrieve_messages(gammu_state_t **sp,
                             int argc, char *argv[]) {

  int rv = 0;
  
  /* Lazy initialization of libgammu */
  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    print_operation_error(OP_ERR_INIT);
    rv = 1; goto cleanup;
  }

  if (!print_messages_json_utf8(s)) {
    print_operation_error(OP_ERR_RETRIEVE);
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

  printf("\"totals\": { ");

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

  printf(" }, ");

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
      fatal(123, "unhandled deletion result %d", result);
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

  printf("\"detail\": { ");

  boolean_t rv = for_each_message(
    s, _before_deletion_callback, (void *) &status
  );

  printf(" }, ");

  /* JSON summary output */
  print_deletion_status_json_utf8(&status);
  return rv;
}

/**
 * @name action_delete_messages:
 */
int action_delete_messages(gammu_state_t **sp,
                           int argc, char *argv[]) {

  int rv = 0;
  bitfield_t *bf = NULL;

  if (argc < 2) {
    print_usage_error(U_ERR_LOC_MISSING);
    return 1;
  }

  int delete_all = (strcmp(argv[1], "all") == 0);

  if (!delete_all) {

    unsigned long n;
    boolean_t found = find_maximum_integer_argument(&n, &argv[1]);

    if (!found) {
      print_usage_error(U_ERR_LOC_INVAL);
      rv = 2; goto cleanup;
    }

    if (n == ULONG_MAX && errno == ERANGE) {
      print_usage_error(U_ERR_OVERFLOW);
      rv = 3; goto cleanup;
    }

    bf = bitfield_create(n);

    if (!bf) {
      print_operation_error(OP_ERR_INDEX);
      rv = 4; goto cleanup_delete;
    }

    if (!bitfield_set_integer_arguments(bf, &argv[1])) {
      print_operation_error(OP_ERR_LOCATION);
      rv = 5; goto cleanup_delete;
    }
  }

  /* Lazy initialization of libgammu */
  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    print_operation_error(OP_ERR_INIT);
    rv = 6; goto cleanup_delete;
  }

  printf("{ ");

  if (!delete_selected_messages(s, bf)) {
    print_operation_error(OP_ERR_DELETE);
    rv = 7; goto cleanup_json;
  }

  cleanup_json:
    printf(" }\n");

  cleanup_delete:
    if (bf) {
      bitfield_destroy(bf);
    }

  cleanup:
    return rv;
}

/** --- **/

/**
 * @name print_json_validation_error:
 */
void print_json_validation_error(json_validation_error_t err) {

  const char *s = json_validation_error_text(err);

  if (app.repl) {
    print_repl_error(err, s);
  } else {
    fprintf(stderr, "Error: %s.\n", s);
    fprintf(stderr, "Failure while parsing/validating JSON.\n");
  }
}

/**
 * @name print_json_transmit_status:
 */
void print_json_transmit_status(gammu_state_t *s, multimessage_t *m,
                                transmit_status_t *t, boolean_t is_start) {

  if (!is_start) {
    printf(", ");
  }

  printf("{ ");
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

      printf("{ ");

      if (t->parts[i].err) {
        printf("\"result\": \"error\", ");
        printf("\"error\": \"%s\", ", t->parts[i].err); /* const */
      } else {
        char *text =
          utf16be_encode_json_utf8((char *) m->SMS[i].Text);

        printf("\"result\": \"success\", ");
        printf("\"content\": \"%s\", ", text);
        free(text);
      }

      printf("\"index\": %d, ", i + 1);
      printf("\"status\": %d, ", t->parts[i].status);
      printf("\"reference\": %d", t->parts[i].reference);

      printf(" }");
    }

    printf("]");
  }

  printf(" }");
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
int action_send_messages(gammu_state_t **sp,
                         int argc, char *argv[]) {

  int rv = 0;
  char **argp = &argv[1];

  if (argc <= 2) {
    print_usage_error(U_ERR_ARGS_MISSING);
    return 1;
  }

  if (argc % 2 != 1) {
    print_usage_error(U_ERR_ARGS_ODD);
    return 2;
  }

  /* Lazy initialization of libgammu */
  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    print_operation_error(OP_ERR_INIT);
    rv = 3; goto cleanup;
  }

  /* Allocate */
  smsc_t *smsc = allocate(sizeof(*smsc));
  multimessage_t *sms = allocate(sizeof(*sms));
  multimessage_info_t *info = allocate(sizeof(*info));

  /* Find SMSC number */
  smsc->Location = 1;

  if ((s->err = GSM_GetSMSC(s->sm, smsc)) != ERR_NONE) {
    print_operation_error(OP_ERR_SMSC);
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

    /* Copy/convert destination phone number */
    char *sms_destination_number = convert_utf8_utf16be(*argp++, FALSE);

    if (!sms_destination_number) {
      status.err = "Invalid UTF-8 sequence in destination number";
      goto cleanup_end;
    }

    string_info_t nsi;
    utf16be_string_info(sms_destination_number, &nsi);

    /* Check size of phone number:
        We'll be decoding this in to a fixed-sized buffer. */

    if (nsi.units >= GSM_MAX_NUMBER_LENGTH) {
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
    char *sms_message = *argp++;

    /* Convert message from UTF-8 to UTF-16-BE:
        Every symbol is two bytes long; the string is then
        terminated by a single 2-byte UTF-16 null character. */

    char *sms_message_utf16be = convert_utf8_utf16be(sms_message, FALSE);

    if (!sms_message_utf16be) {
      status.err = "Invalid UTF-8 sequence";
      goto cleanup_transmit_status;
    }

    /* Prepare message info structure:
        This information is used to encode the possibly-multipart SMS. */

    info->Class = 1;
    info->EntriesNum = 1;
    info->Entries[0].ID = SMS_ConcatenatedTextLong;
    info->Entries[0].Buffer = (uint8_t *) sms_message_utf16be;
    info->UnicodeCoding = !utf16be_is_gsm_string(sms_message_utf16be);

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

      CopyUnicodeString(
        sms->SMS[i].Number, (unsigned char *) sms_destination_number
      );

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
      free(sms_message_utf16be);

    cleanup_transmit_status:
      print_json_transmit_status(s, sms, &status, is_start);
      free(sms_destination_number);

    cleanup_end:
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
        print_usage_error(U_ERR_CONFIG_MISSING);
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

      /* FIXME: Remove this when REPL mode is stable */
      warn("-r/--repl is experimental code");

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
boolean_t process_command(gammu_state_t **s,
                          int argc, char *argv[], int *rv) {

  *rv = 0;

  /* Option #1:
   *   Retrieve all messages as a JSON array. */

  if (argc > 0 && strcmp(argv[0], "retrieve") == 0) {
    *rv = action_retrieve_messages(s, argc, argv);
    return TRUE;
  }

  /* Option #2:
   *   Delete messages specified in `argv` (or all messages). */

  if (argc > 0 && strcmp(argv[0], "delete") == 0) {
    *rv = action_delete_messages(s, argc, argv);
    return TRUE;
  }

  /* Option #3:
   *   Send one or more messages, each to a single recipient. */

  if (argc > 0 && strcmp(argv[0], "send") == 0) {
    *rv = action_send_messages(s, argc, argv);
    return TRUE;
  }

  return FALSE;
}

/**
 * @name process_repl_commands:
 */
void process_repl_commands(gammu_state_t **s, FILE *stream) {

  for (;;) {

    boolean_t is_eof = FALSE;
    char *line = read_line(stream, &is_eof);

    if (!line) {
      break;
    }

    if (is_eof && line[0] == '\0') {
      goto cleanup;
    }

    parsed_json_t *p = parse_json(line);

    if (p) {

      char **argv = NULL;
      int argc = 0, err = 0;

      boolean_t rv = parsed_json_to_arguments(p, &argc, &argv, &err);

      if (!rv) {
        print_json_validation_error(err);
        goto cleanup_json;
      }

      int result = 0;

      if (!process_command(s, argc, argv, &result)) {

        if (!result) {
          print_usage_error(U_ERR_CMD_INVAL);
        }
        goto cleanup_json;
      }

    } else {
      print_json_validation_error(V_ERR_PARSE);
    }

    cleanup_json:

      if (p) {
        release_parsed_json(p);
      }

    cleanup:

      free(line);

      if (is_eof) {
        break;
      }
  }
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
    print_usage_error(U_ERR_ARGS_INVAL);
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
    if (!process_command(&s, argc, argp, &rv)) {
      print_usage_error(U_ERR_CMD_INVAL);
    }
  } else if (!app.repl) {
    print_usage_error(U_ERR_CMD_MISSING);
    goto cleanup;
  }

  /* Read, execute, print loop:
   *  Repeatedly read lines of JSON from standard input, parse
   *  them in to command/arguments tuples, dispatch these tuples
   *  to `process_command`, and repeat until reaching end-of-file. */

  if (app.repl) {
    process_repl_commands(&s, stdin);
  }

  cleanup:

    if (s) {
      gammu_destroy(s);
    }

    return rv;
}

/* vim: set ts=4 sts=2 sw=2 expandtab: */
