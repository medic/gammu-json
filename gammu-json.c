
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>

#include <gammu.h>

#define TIMESTAMP_MAX_WIDTH (64)
#define BITFIELD_CELL_WIDTH (CHAR_BIT)

static char *application_name;

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
 * @name boolean_t
 */
typedef uint8_t boolean_t;

/**
 * @name message_iterate_fn_t
 */
typedef int (*message_iterate_fn_t)(
  gammu_state_t *, multimessage_t *, int, void *
);

/**
 * @name bitfield_t
 */
typedef struct bitfield {

  unsigned int n;
  uint8_t *data;

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

  for (int i = 0; i < GSM_MAX_MULTI_SMS; i++) {
    t->parts[i].err = NULL;
    t->parts[i].status = 0;
    t->parts[i].reference = 0;
    t->parts[i].transmitted = FALSE;
  }

  return t;
}

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
 *   Return true if the one-based bit `bit` is set. Returns true
 *   on success, false if `bit` is out of range for this bitfield.
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
 *   Set the one-based bit `bit` to one if `value` is non-zero,
 *   otherwise set the bit to zero. Returns true on success, false
 *   if the bit `bit` is out of range for this particular bitfield.
 */
boolean_t bitfield_set(bitfield_t *bf, unsigned long bit, int value) {

  if (bit > bf->n) {
    return FALSE;
  }

  unsigned long cell = (bit / BITFIELD_CELL_WIDTH);
  unsigned long offset = (bit % BITFIELD_CELL_WIDTH);

  bf->data[cell] |= (1 << offset);
  return TRUE;
}

/**
 * @name ucs2_encode_json_utf8:
 *   Copy and transform the string `s` to a newly-allocated
 *   buffer, making it suitable for output as a single utf-8
 *   JSON string. The caller must free the returned string.
 */
char *ucs2_encode_json_utf8(const unsigned char *s) {

  int i, j = 0;
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

  for (int i = 0; i < ul; ++i) {
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
    return s;
  }

  GSM_Config *cfg = GSM_GetConfig(s->sm, 0);

  if ((s->err = GSM_ReadConfig(ini, cfg, 0)) != ERR_NONE) {
    return s;
  }

  INI_Free(ini);
  GSM_SetConfigNum(s->sm, 1);

  if ((s->err = GSM_InitConnection(s->sm, 1)) != ERR_NONE) {
    return s;
  }

  return s;
}

/**
 * @name gammu_destroy:
 */
void gammu_destroy(gammu_state_t *s) {

  GSM_TerminateConnection(s->sm);
  GSM_FreeStateMachine(s->sm);

  free(s);
}

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

  for (int i = 0; i < sms->Number; i++) {

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
    if (sms->SMS[i].UDH.Type != UDH_NoUDH) {
      if (sms->SMS[i].UDH.ID16bit != -1) {
        printf("\"udh\": %d, ", sms->SMS[i].UDH.ID16bit);
      } else if (sms->SMS[i].UDH.ID16bit != -1) {
        printf("\"udh\": %d, ", sms->SMS[i].UDH.ID8bit);
      } else {
        printf("\"udh\": false, ");
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
 * @name delete_single_message:
 */
boolean_t delete_single_message(gammu_state_t *s,
                                multimessage_t *sms,
                                boolean_t is_start, void *x) {

  bitfield_t *bf = (bitfield_t *) x;

  for (int i = 0; i < sms->Number; i++) {

    if (bf && !bitfield_test(bf, sms->SMS[i].Location)) {
      continue;
    }

    if ((s->err = GSM_DeleteSMS(s->sm, &sms->SMS[i])) != ERR_NONE) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * @name delete_selected_messages:
 */
boolean_t delete_selected_messages(gammu_state_t *s, bitfield_t *bf) {

  boolean_t rv = for_each_message(
    s, (message_iterate_fn_t) delete_single_message, (void *) bf
  );

  return rv;
}

/**
 * @name usage:
 */
int usage() {

  fprintf(
    stderr, "Usage: %s { retrieve | send { phone text }... | delete N... }\n",
      application_name
  );

  return 127;
}

/**
 * @name find_maximum_integer_argument:
 */
unsigned long find_maximum_integer_argument(char *argv[]) {

  unsigned int rv = 0;

  for (int i = 0; argv[i] != NULL; i++) {

    char *err = NULL;
    unsigned long n = strtoul(argv[i], &err, 10);

    if (err == NULL || *err != '\0') {
      continue;
    }

    if (n > 0 && n > rv) {
      rv = n;
    }
  }

  return rv;
}

/**
 * @name bitfield_set_integer_arguments:
 */
boolean_t bitfield_set_integer_arguments(bitfield_t *bf, char *argv[]) {

  for (int i = 0; argv[i] != NULL; i++) {

    char *err = NULL;
    unsigned long n = strtoul(argv[i], &err, 10);

    if (err == NULL || *err != '\0') {
      return FALSE;
    }

    if (!bitfield_set(bf, n, 1)) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * @name gammu_create_if_necessary:
 */
gammu_state_t *gammu_create_if_necessary(gammu_state_t **sp) {

  if (*sp != NULL) {
    return *sp;
  }

  gammu_state_t *rv = gammu_create(NULL);

  if (!rv) {
    return NULL;
  }

  *sp = rv;
  return rv;
}

/**
 * @name action_retrieve_messages:
 */
int action_retrieve_messages(gammu_state_t **sp, int argc, char *argv[]) {

  int rv = 0;
  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    fprintf(stderr, "Error: failed to start gammu\n");
    rv = 1; goto cleanup;
  }

  if (!print_messages_json_utf8(s)) {
    fprintf(stderr, "Error: failed to retrieve one or more messages\n");
    rv = 2; goto cleanup;
  }

  cleanup:
    return rv;
}

/**
 * @name print_json_delete_status:
 */
void print_json_delete_status(gammu_state_t *s) {

}

/**
 * @name action_delete_messages:
 */
int action_delete_messages(gammu_state_t **sp, int argc, char *argv[]) {

  int rv = 0;
  bitfield_t *bf = NULL;

  if (argc < 2) {
    return usage();
  }

  int delete_all = (strcmp(argv[1], "all") == 0);

  if (!delete_all) {

    unsigned long n = find_maximum_integer_argument(&argv[1]);

    if (!n) {
      fprintf(stderr, "Error: no valid location(s) specified\n");
      rv = usage();
      goto cleanup;
    }
    
    if (n == ULONG_MAX && errno == ERANGE) {
      fprintf(stderr, "Error: integer argument would overflow\n");
      rv = 3; goto cleanup;
    }

    bf = bitfield_create(n);

    if (!bf) {
      fprintf(stderr, "Error: failed to create deletion index\n");
      rv = 4; goto cleanup_delete;
    }

    if (!bitfield_set_integer_arguments(bf, &argv[1])) {
      fprintf(stderr, "Error: failed to add item(s) to deletion index\n");
      rv = 5; goto cleanup_delete;
    }
  }

  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    fprintf(stderr, "Error: failed to initialize gammu\n");
    rv = 6; goto cleanup_delete;
  }

  if (!delete_selected_messages(s, bf)) {
    fprintf(stderr, "Error: failed to delete one or more messages\n");
    rv = 7; goto cleanup_delete;
  }

  cleanup_delete:
    if (bf) {
      bitfield_destroy(bf);
    }
  cleanup:
    return rv;
}


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
    for (int i = 0; i < t->parts_total; i++) {

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
    return usage();
  }

  if (argc % 2 != 1) {
    fprintf(stderr, "Error: Odd number of arguments provided\n");
    return usage();
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
    fprintf(stderr, "Error: Failed to start gammu subsystem\n");
    rv = 1; goto cleanup;
  }

  /* Find SMSC number */
  smsc->Location = 1;

  if ((s->err = GSM_GetSMSC(s->sm, smsc)) != ERR_NONE) {
    fprintf(stderr, "Error: Failed to discover SMSC number\n");
    rv = 2; goto cleanup_sms;
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

    /* Prepare message info structure::
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
    for (int i = 0; i < sms->Number; i++) {

      status.finished = FALSE;
      status.message_part_index = i;

      /* Copy destination phone number:
           This is a fixed-size buffer; size was already checked above. */

      sms->SMS[i].PDU = SMS_Submit;
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

  cleanup:
    printf("]\n");
    return rv;
}

/**
 * @name main:
 */
int main(int argc, char *argv[]) {

  int rv = 0;
  gammu_state_t *s = NULL;
  /* global */ application_name = argv[0];

  if (argc <= 1) {
    return usage();
  }

  /* Option #1:
   *   Retrieve all messages as a JSON array. */

  if (strcmp(argv[1], "retrieve") == 0) {
    rv = action_retrieve_messages(&s, argc, argv);
    goto cleanup;
  }

  /* Option #2:
   *   Delete messages specified in `argv` (or all messages). */

  if (strcmp(argv[1], "delete") == 0) {
    rv = action_delete_messages(&s, argc - 1, &argv[1]);
    goto cleanup;
  }

  /* Option #3:
   *   Send one or more messages, each to a single recipient. */

  if (strcmp(argv[1], "send") == 0) {
    rv = action_send_messages(&s, argc - 1, &argv[1]);
    goto cleanup;
  }

  /* No other valid options:
   *  Display message and usage information. */

  fprintf(stderr, "Error: invalid action specified\n");
  rv = usage();

  cleanup:
    if (s) {
      gammu_destroy(s);
    }
    return rv;
}

