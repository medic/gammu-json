
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
 * @name transmit_status_t
 */
typedef struct transmit_status {

  int finished;
  int last_send_status;
  int reference_number;

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
transmit_status_t *initialize_transmit_status(transmit_status_t *s) {

  s->finished = FALSE;
  return s;
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
int bitfield_test(bitfield_t *bf, unsigned long bit) {

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
int bitfield_set(bitfield_t *bf, unsigned long bit, int value) {

  if (bit > bf->n) {
    return FALSE;
  }

  unsigned long cell = (bit / BITFIELD_CELL_WIDTH);
  unsigned long offset = (bit % BITFIELD_CELL_WIDTH);

  bf->data[cell] |= (1 << offset);
  return TRUE;
}

/**
 * @name encode_json_utf8:
 */
char *encode_json_utf8(const unsigned char *ucs2_str) {

  int i, j = 0;
  int ul = UnicodeLength(ucs2_str);

  /* Worst-case UCS-2 string allocation:
   *  Original length plus null terminator; two bytes for each
   *  character; every character escaped with a UCS-2 backslash. */

  unsigned char *b =
    (unsigned char *) malloc_and_zero((ul + 1) * 2 * 2);

  for (i = 0; i < ul; ++i) {
    char msb = ucs2_str[2 * i], lsb = ucs2_str[2 * i + 1];
    
    if (msb == '\0') {
      char escape = '\0';

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
 * @name is_empty_timestamp:
 */
int is_empty_timestamp(message_timestamp_t *t) {

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
int for_each_message(gammu_state_t *s,
                     message_iterate_fn_t fn, void *x) {
  int rv = FALSE;
  int start = TRUE;

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
int print_message_json_utf8(gammu_state_t *s,
                            multimessage_t *sms, int is_start, void *x) {
  if (!is_start) {
    printf(", ");
  }

  for (int i = 0; i < sms->Number; i++) {

    printf("{");

    /* Modem file/location information */
    printf("\"folder\": %d, ", sms->SMS[i].Folder);
    printf("\"location\": %d, ", sms->SMS[i].Location);

    /* Originating phone number */
    char *from = encode_json_utf8(sms->SMS[i].Number);
    printf("\"from\": \"%s\", ", from);
    free(from);

    /* Phone number of telco's SMS service center */
    char *smsc = encode_json_utf8(sms->SMS[i].SMSC.Number);
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
      printf("\"smsc-timestamp\": false, ");
    } else {
      char *smsc_timestamp = encode_timestamp_utf8(&sms->SMS[i].SMSCTime);
      printf("\"smsc-timestamp\": \"%s\", ", smsc_timestamp);
      free(smsc_timestamp);
    }

    /* Multi-part message metadata */
    int parts = sms->SMS[i].UDH.AllParts;
    int part = sms->SMS[i].UDH.PartNumber;

    printf("\"segment\": %d, ", (part > 0 ? part : 1));
    printf("\"total-segments\": %d, ", (parts > 0 ? parts : 1));

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
        char *text = encode_json_utf8(sms->SMS[i].Text);
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

  return TRUE;
}

/**
 * @name print_messages_json_utf8:
 */
int print_messages_json_utf8(gammu_state_t *s) {

  printf("[");

  int rv = for_each_message(
    s, (message_iterate_fn_t) print_message_json_utf8, NULL
  );

  printf("]\n");
  return rv;
}

/**
 * @name delete_single_message:
 */
int delete_single_message(gammu_state_t *s,
                          multimessage_t *sms, int is_start, void *x) {

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
int delete_selected_messages(gammu_state_t *s, bitfield_t *bf) {

  int rv = for_each_message(
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
int bitfield_set_integer_arguments(bitfield_t *bf, char *argv[]) {

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
 * @name _message_transmit_callback:
 */
void _message_transmit_callback(GSM_StateMachine *sm,
                                int status, int ref, void *x) {

  transmit_status_t *s = (transmit_status_t *) x;

  s->finished = TRUE;
  s->reference_number = ref;
  s->last_send_status = status;
  
  fprintf(stderr, "_message_transmit_callback: %d %d\n", status, ref);
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

  smsc_t *smsc =
    (smsc_t *) malloc_and_zero(sizeof(*smsc));

  multimessage_t *sms =
    (multimessage_t *) malloc_and_zero(sizeof(*sms));

  multimessage_info_t *info =
    (multimessage_info_t *) malloc_and_zero(sizeof(*info));

  /* Lazy initialization of libgammu */
  gammu_state_t *s = gammu_create_if_necessary(sp);

  if (!s) {
    fprintf(stderr, "Error: failed to start gammu\n");
    rv = 1; goto cleanup;
  }

  /* Find SMSC number */
  smsc->Location = 1;

  if ((s->err = GSM_GetSMSC(s->sm, smsc)) != ERR_NONE) {
    rv = 2; goto cleanup_sms;
  }

  transmit_status_t status;
  initialize_transmit_status(&status);

  GSM_SetSendSMSStatusCallback(
    s->sm, _message_transmit_callback, &status
  );

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

    if (nl.bytes > 24) {
      fprintf(
        stderr, "Error: Phone number `%s' is too long\n",
          sms_destination_number
      );
      rv = 3; goto cleanup_sms;
    }

    /* Missing message text:
        This shouldn't happen since we check `argc` above,
        but I'm leaving this here in case we refactor later. */

    if (*argp == NULL) {
      fprintf(
        stderr, "Error: no message body provided for `%s'\n",
          sms_destination_number
      );
      rv = 4; goto cleanup_sms;
      break;
    }

    /* UTF-8 message content */
    utf8_length_info_t ml;
    char *sms_message = *argp++;
    utf8_string_length(sms_message, &ml);

    /* Convert message from UTF-8 to UCS-2:
        Every symbol is two bytes long; the string is then
        terminated by a single 2-byte UCS-2 null character. */

    unsigned char *sms_message_unicode =
      (unsigned char *) malloc_and_zero((ml.symbols + 1) * 2);

    DecodeUTF8(sms_message_unicode, sms_message, ml.bytes);

    /* Prepare message info structure::
        This information is used to encode the possibly-multipart SMS. */

    info->Class = 1;
    info->EntriesNum = 1;
    info->UnicodeCoding = FALSE;
    info->Entries[0].Buffer = sms_message_unicode;
    info->Entries[0].ID = SMS_ConcatenatedTextLong;

    if ((s->err = GSM_EncodeMultiPartSMS(debug, info, sms)) != ERR_NONE) {
      fprintf(stderr, "Error: Failed to encode message");
      rv = 5; goto cleanup_sms_text;
    }

    /* For each SMS part... */
    for (int i = 0; i < sms->Number; i++) {

      status.finished = FALSE;
      sms->SMS[i].PDU = SMS_Submit;

      /* Copy destination phone number:
           This is a fixed-size buffer; size was already checked above. */

      CopyUnicodeString(sms->SMS[i].SMSC.Number, smsc->Number);
      DecodeUTF8(sms->SMS[i].Number, sms_destination_number, nl.bytes);

      /* Transmit a single message part */
      if ((s->err = GSM_SendSMS(s->sm, &sms->SMS[i])) != ERR_NONE) {
        rv = 6; goto cleanup_sms_text;
      }

      /* Wait for reply */
      for (;;) {
        GSM_ReadDevice(s->sm, TRUE);

        if (status.finished) {
          break;
        }
      }
    }

    cleanup_sms_text:
      free(sms_message_unicode);
  }

  cleanup_sms:
    free(sms);
    free(smsc);
    free(info);

  cleanup:
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

