
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <gammu.h>

/**
 * @name sms_iterate_fn_t
 */
typedef int (*sms_iterate_fn_t)(GSM_MultiSMSMessage *, void *);


/**
 * @name gammu_state_t
 */
typedef struct gammu_state {

  int errno;
  GSM_StateMachine *sm;

} gammu_state_t;


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

  char *rv =
    (char *) malloc_and_zero(UnicodeLength(b) + 1);

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
 * @name for_each_sms_message:
 */
int for_each_sms_message(gammu_state_t *s,
			 sms_iterate_fn_t fn, void *x) {
  int rv = FALSE;
  int start = TRUE;

  GSM_MultiSMSMessage *sms =
    (GSM_MultiSMSMessage *) malloc_and_zero(sizeof(*sms));

  printf("[");

  for (;;) {

    int err = GSM_GetNextSMS(s->sm, sms, start);

    if (err == ERR_EMPTY) {
      rv = TRUE;
      break;
    }

    if (err != ERR_NONE) {
      break;
    }

    if (!start) {
      printf(", ");
    }

    rv = TRUE;
    start = FALSE;
    
    if (!fn(sms, x)) {
      break;
    }
  }

  free(sms);
  return rv;
}

int print_sms_json_utf8(GSM_MultiSMSMessage *sms) {

  for (int i = 0; i < sms->Number; i++) {

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

  return TRUE;
}

/**
 * @name print_all_sms_json_utf8:
 */
int print_all_sms_json_utf8(gammu_state_t *s) {

  printf("[");

  int rv = for_each_sms_message(
    s, (sms_iterate_fn_t) print_sms_json_utf8, NULL
  );

  printf("]\n");
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

  int rv = 111;

  if (argc < 2) {
    return usage(argc, argv);
  }

  gammu_state_t *s = gammu_create(NULL);

  if (!s) {
    return 1;
  }

  if (strcmp(argv[1], "retrieve") == 0) {
    print_all_sms_json_utf8(s);
  }

  gammu_free(s);
  return rv;
}

