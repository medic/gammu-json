
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <gammu.h>


/**
 * @name gammu_state_t
 */
typedef struct gammu_state {

  int errno;
  GSM_StateMachine *sm;

} gammu_state_t;

/**
 * @name message_t
 */
typedef GSM_MultiSMSMessage message_t;

/**
 * @name message_iterate_fn_t
 */
typedef int (*message_iterate_fn_t)(
  gammu_state_t *, message_t *, int, void *
);

/**
 * @name malloc_and_zero
 */
static void *malloc_and_zero(int size) {

  void *rv = malloc(size);

  memset(rv, '\0', size);
  return rv;
}

/**
 * @name encode_json_utf8:
 */
char *encode_json_utf8(const unsigned char *ucs2_str) {

  int i, j = 0;
  int ul = UnicodeLength(ucs2_str);

  unsigned char *b =
    (unsigned char *) malloc_and_zero((2 * ul) * 2 + 2);

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
   *  Four bytes per character (see RFC3629), plus 1-byte NUL. */

  char *rv = (char *) malloc_and_zero(4 * UnicodeLength(b) + 1);

  EncodeUTF8(rv, b);
  free(b);

  return rv;
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

  s->errno = ERR_NONE;
  s->sm = GSM_AllocStateMachine();

  if ((s->errno = GSM_FindGammuRC(&ini, config_path)) != ERR_NONE) {
    return s;
  }

  GSM_Config *cfg = GSM_GetConfig(s->sm, 0);

  if ((s->errno = GSM_ReadConfig(ini, cfg, 0)) != ERR_NONE) {
    return s;
  }

  INI_Free(ini);
  GSM_SetConfigNum(s->sm, 1);

  if ((s->errno = GSM_InitConnection(s->sm, 1)) != ERR_NONE) {
    return s;
  }

  return s;
}

/**
 * @name gammu_free:
 */
void gammu_free(gammu_state_t *s) {

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

  message_t *sms =
    (message_t *) malloc_and_zero(sizeof(*sms));

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
int print_message_json_utf8(gammu_state_t *s, message_t *sms, int first) {

  if (!first) {
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

    /* Multi-part message metadata */
    printf("\"segment\": %d, ", sms->SMS[i].UDH.PartNumber);
    printf("\"total-segments\": %d, ", sms->SMS[i].UDH.AllParts);

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

    printf("\"complete\": true");
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
int delete_single_message(gammu_state_t *s, message_t *sms) {

  for (int i = 0; i < sms->Number; i++) {
    if ((s->errno = GSM_DeleteSMS(s->sm, &sms->SMS[i])) != ERR_NONE) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * @name delete_selected_messages:
 */
int delete_selected_messages(gammu_state_t *s) {

  int rv = for_each_message(
    s, (message_iterate_fn_t) delete_single_message, NULL
  );

  return rv;
}

/**
 * @name usage:
 */
int usage(int argc, char *argv[]) {

  printf("Usage: %s [ retrieve | delete message_number... ]\n", argv[0]);
  return 127;
}

/**
 * @name main:
 */
int main(int argc, char *argv[]) {

  int rv = 0;

  if (argc < 2) {
    return usage(argc, argv);
  }

  gammu_state_t *s = gammu_create(NULL);

  if (!s) {
    rv = 1;
    goto cleanup;
  }

  /* Retrieve messages as JSON */
  if (strcmp(argv[1], "retrieve") == 0) {

    if (!print_messages_json_utf8(s)) {
      rv = 2;
    }

    goto cleanup;
  }

  /* Delete specified messages (or all) */
  if (strcmp(argv[1], "delete") == 0) {

    if (!delete_selected_messages(s)) {
      rv = 3;
    }

    goto cleanup;
  }

  cleanup:
    gammu_free(s);
    return rv;
}

