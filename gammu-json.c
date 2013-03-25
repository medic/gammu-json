
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <gammu.h>

/**
 * @name malloc_and_zero
 */
static void *malloc_and_zero(int size) {

  void *rv = malloc(size);

  memset(rv, '\0', size);
  return rv;
}

/**
 * @name make_json_utf8:
 */
char *make_json_utf8(const unsigned char *ucs2_str) {

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

      if (escape) {
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

  char *rv = (char *) malloc_and_zero(UnicodeLength(b) + 1);

  EncodeUTF8(rv, b);
  free(b);

  return rv;
}

/**
 * @name retrieve_sms_all:
 */
int retrieve_sms_all() {

  INI_Section *ini;
  GSM_InitLocales(NULL);

  GSM_StateMachine *s = GSM_AllocStateMachine();
  GSM_MultiSMSMessage *sms = (GSM_MultiSMSMessage *) malloc(sizeof(*sms));

  if (GSM_FindGammuRC(&ini, NULL) != ERR_NONE) {
    return 1;
  }

  if (GSM_ReadConfig(ini, GSM_GetConfig(s, 0), 0) != ERR_NONE) {
    return 2;
  }

  INI_Free(ini);
  GSM_SetConfigNum(s, 1);

  if (GSM_InitConnection(s, 1) != ERR_NONE) {
    return 3;
  }

  int i;
  int start = TRUE;

  printf("[");

  for (;;) {

    int err = GSM_GetNextSMS(s, sms, start);

    if (err == ERR_EMPTY) {
      break;
    }

    if (err != ERR_NONE) {
      return 4;
    }

    if (!start) {
      printf(", ");
    }
    
    start = FALSE;

    for (i = 0; i < sms->Number; i++) {

      printf("{");

      printf("\"folder\": %d, ", sms->SMS[i].Folder);
      printf("\"location\": %d, ", sms->SMS[i].Location);

      char *from = make_json_utf8(sms->SMS[i].Number);
      printf("\"from\": \"%s\", ", from);
      free(from);

      char *smsc = make_json_utf8(sms->SMS[i].SMSC.Number);
      printf("\"smsc\": \"%s\", ", smsc);
      free(smsc);

      printf("\"segment\": %d, ", sms->SMS[i].UDH.PartNumber);
      printf("\"total-segments\": %d, ", sms->SMS[i].UDH.AllParts);

      if (sms->SMS[i].UDH.Type != UDH_NoUDH) {
        if (sms->SMS[i].UDH.ID16bit != -1) {
          printf("\"udh\": %d, ", sms->SMS[i].UDH.ID16bit);
        } else {
          printf("\"udh\": %d, ", sms->SMS[i].UDH.ID8bit);
        }
      }

      char *text = make_json_utf8(sms->SMS[i].Text);
      printf("\"content\": \"%s\"", text);
      free(text);

      printf("}");
    }

  }

  printf("]\n");

  if (GSM_TerminateConnection(s) != ERR_NONE) {
    return 127;
  }

  GSM_FreeStateMachine(s);
  free(sms);

  return 0;
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

  if (argc < 2) {
    return usage(argc, argv);
  }

  if (strcmp(argv[1], "retrieve") == 0) {
    return retrieve_sms_all();
  }

  return 111;
}

