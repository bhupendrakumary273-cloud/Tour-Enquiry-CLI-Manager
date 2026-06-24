/*
 * ================================================================
 *  Tour Enquiry CLI Manager
 *  Tech: C, Standard I/O (stdio.h, stdlib.h)
 *  Features: CRUD operations, binary .dat persistence, search,
 *            status tracking, input validation, colored UI
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ─── Cross-platform timer (not used for output, but for portability) ── */
#ifdef _WIN32
  #include <windows.h>
  #define CLEAR_SCREEN "cls"
#else
  #define CLEAR_SCREEN "clear"
#endif

/* ─── Constants ──────────────────────────────────────────── */

#define DATA_FILE        "enquiries.dat"
#define MAX_NAME         60
#define MAX_PHONE        20
#define MAX_EMAIL        80
#define MAX_DESTINATION  80
#define MAX_NOTES        200
#define MAX_DATE         12    /* DD/MM/YYYY\0 */
#define MAX_ENQUIRIES    500
#define MAX_TMP          210   /* largest field (MAX_NOTES) + safety */

/* ─── ANSI Colors ────────────────────────────────────────── */

#ifdef _WIN32
  #define COL_RESET   ""
  #define COL_BOLD    ""
  #define COL_CYAN    ""
  #define COL_YELLOW  ""
  #define COL_GREEN   ""
  #define COL_RED     ""
  #define COL_MAGENTA ""
  #define COL_BLUE    ""
  #define COL_WHITE   ""
#else
  #define COL_RESET   "\033[0m"
  #define COL_BOLD    "\033[1m"
  #define COL_CYAN    "\033[1;36m"
  #define COL_YELLOW  "\033[1;33m"
  #define COL_GREEN   "\033[1;32m"
  #define COL_RED     "\033[1;31m"
  #define COL_MAGENTA "\033[1;35m"
  #define COL_BLUE    "\033[1;34m"
  #define COL_WHITE   "\033[1;37m"
#endif

/* ─── Enumerations ───────────────────────────────────────── */

typedef enum {
    STATUS_NEW = 0,
    STATUS_CONTACTED,
    STATUS_CONFIRMED,
    STATUS_CANCELLED,
    STATUS_COMPLETED
} EnquiryStatus;

typedef enum {
    BUDGET_ECONOMY = 0,
    BUDGET_STANDARD,
    BUDGET_PREMIUM,
    BUDGET_LUXURY
} BudgetCategory;

/* ─── Core Data Structure ────────────────────────────────── */

typedef struct {
    int             id;
    char            name[MAX_NAME];
    char            phone[MAX_PHONE];
    char            email[MAX_EMAIL];
    char            destination[MAX_DESTINATION];
    char            travel_date[MAX_DATE];
    char            return_date[MAX_DATE];
    int             num_travelers;
    BudgetCategory  budget;
    EnquiryStatus   status;
    char            notes[MAX_NOTES];
    char            created_on[MAX_DATE];
    int             is_deleted;
} Enquiry;

/* ─── File Header ─────────────────────────────────────────── */

typedef struct {
    char  magic[8];        /* "TOURDAT\0" */
    int   version;         /* 1           */
    int   next_id;
    int   total_records;
} FileHeader;

/* ─── Globals ────────────────────────────────────────────── */

static int g_next_id       = 1;
static int g_total_records = 0;

/* ══════════════════════════════════════════════════════════
   UTILITY HELPERS
   ══════════════════════════════════════════════════════════ */

/* Safe string input — reads a full line, strips newline */
static void input_string(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    fflush(stdout);
    buf[0] = '\0';
    if (fgets(buf, size, stdin) != NULL) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
    }
}

/* Integer input with range validation */
static int input_int(const char *prompt, int lo, int hi) {
    int val;
    char line[32];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        if (fgets(line, (int)sizeof(line), stdin) == NULL) continue;
        if (sscanf(line, "%d", &val) == 1 && val >= lo && val <= hi)
            return val;
        printf("%s  Invalid — enter a number between %d and %d.%s\n",
               COL_RED, lo, hi, COL_RESET);
    }
}

/* Safe fgets wrapper that always returns something */
static void safe_fgets(char *buf, int size) {
    buf[0] = '\0';
    if (fgets(buf, size, stdin) != NULL) {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n')
            buf[l - 1] = '\0';
    }
}

/* Returns today as DD/MM/YYYY */
static void today_string(char *buf) {
    time_t t  = time(NULL);
    struct tm *tm = localtime(&t);
    /* MAX_DATE is 12 — DD/MM/YYYY\0 = 11 chars, safe */
    snprintf(buf, MAX_DATE, "%02d/%02d/%04d",
             tm->tm_mday, tm->tm_mon + 1,
             1900 + tm->tm_year);
}

/* Validate DD/MM/YYYY (basic sanity, year >= 2024) */
static int valid_date(const char *s) {
    if (!s || strlen(s) != 10)  return 0;
    if (s[2] != '/' || s[5] != '/') return 0;
    int i;
    for (i = 0; i < 10; i++) {
        if (i == 2 || i == 5) continue;
        if (!isdigit((unsigned char)s[i])) return 0;
    }
    int d = atoi(s);
    int m = atoi(s + 3);
    int y = atoi(s + 6);
    return (d >= 1 && d <= 31 && m >= 1 && m <= 12 && y >= 2024);
}

/* Copy at most (dst_size - 1) chars, always null-terminate */
static void safe_copy(char *dst, const char *src, int dst_size) {
    int n = dst_size - 1;
    int i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* Lowercase a string in-place */
static void str_tolower(char *s) {
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

/* Status label */
static const char *status_label(EnquiryStatus s) {
    switch (s) {
        case STATUS_NEW:       return "New";
        case STATUS_CONTACTED: return "Contacted";
        case STATUS_CONFIRMED: return "Confirmed";
        case STATUS_CANCELLED: return "Cancelled";
        case STATUS_COMPLETED: return "Completed";
        default:               return "Unknown";
    }
}

static const char *status_color(EnquiryStatus s) {
    switch (s) {
        case STATUS_NEW:       return COL_CYAN;
        case STATUS_CONTACTED: return COL_YELLOW;
        case STATUS_CONFIRMED: return COL_GREEN;
        case STATUS_CANCELLED: return COL_RED;
        case STATUS_COMPLETED: return COL_MAGENTA;
        default:               return COL_RESET;
    }
}

static const char *budget_label(BudgetCategory b) {
    switch (b) {
        case BUDGET_ECONOMY:  return "Economy";
        case BUDGET_STANDARD: return "Standard";
        case BUDGET_PREMIUM:  return "Premium";
        case BUDGET_LUXURY:   return "Luxury";
        default:              return "Unknown";
    }
}

/* ══════════════════════════════════════════════════════════
   FILE I/O
   ══════════════════════════════════════════════════════════
   Layout:
     [FileHeader]                     @ byte 0
     [Enquiry_0][Enquiry_1]...        @ byte sizeof(FileHeader)
   Record slot i:
     offset = sizeof(FileHeader) + i * sizeof(Enquiry)
   ══════════════════════════════════════════════════════════ */

static int write_header(FILE *fp) {
    FileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    safe_copy(hdr.magic, "TOURDAT", 8);
    hdr.version       = 1;
    hdr.next_id       = g_next_id;
    hdr.total_records = g_total_records;
    rewind(fp);
    return (int)(fwrite(&hdr, sizeof(FileHeader), 1, fp) == 1);
}

static int read_header(FILE *fp) {
    FileHeader hdr;
    rewind(fp);
    if (fread(&hdr, sizeof(FileHeader), 1, fp) != 1) return 0;
    if (strcmp(hdr.magic, "TOURDAT") != 0)           return 0;
    if (hdr.version != 1)                             return 0;
    g_next_id       = hdr.next_id;
    g_total_records = hdr.total_records;
    return 1;
}

static int init_data_file(void) {
    FILE *fp = fopen(DATA_FILE, "wb");
    if (!fp) {
        fprintf(stderr, "%sError: Cannot create %s%s\n",
                COL_RED, DATA_FILE, COL_RESET);
        return 0;
    }
    g_next_id       = 1;
    g_total_records = 0;
    int ok = write_header(fp);
    fclose(fp);
    return ok;
}

static int ensure_data_file(void) {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) {
        printf("%sData file not found — creating %s%s\n",
               COL_YELLOW, DATA_FILE, COL_RESET);
        return init_data_file();
    }
    int ok = read_header(fp);
    fclose(fp);
    if (!ok) {
        printf("%sCorrupt data file — reinitialising...%s\n",
               COL_RED, COL_RESET);
        return init_data_file();
    }
    return 1;
}

static int save_new_enquiry(Enquiry *e) {
    FILE *fp = fopen(DATA_FILE, "r+b");
    if (!fp) return 0;
    long offset = (long)sizeof(FileHeader)
                + (long)g_total_records * (long)sizeof(Enquiry);
    fseek(fp, offset, SEEK_SET);
    int ok = (int)(fwrite(e, sizeof(Enquiry), 1, fp) == 1);
    if (ok) {
        g_total_records++;
        g_next_id = e->id + 1;
        write_header(fp);
    }
    fclose(fp);
    return ok;
}

static int update_record_at(int idx, Enquiry *e) {
    FILE *fp = fopen(DATA_FILE, "r+b");
    if (!fp) return 0;
    long offset = (long)sizeof(FileHeader)
                + (long)idx * (long)sizeof(Enquiry);
    fseek(fp, offset, SEEK_SET);
    int ok = (int)(fwrite(e, sizeof(Enquiry), 1, fp) == 1);
    fclose(fp);
    return ok;
}

static int load_all_enquiries(Enquiry *arr, int max_count) {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) return 0;
    if (!read_header(fp)) { fclose(fp); return 0; }
    int count = 0;
    Enquiry tmp;
    int i;
    for (i = 0; i < g_total_records && count < max_count; i++) {
        if (fread(&tmp, sizeof(Enquiry), 1, fp) == 1 && !tmp.is_deleted)
            arr[count++] = tmp;
    }
    fclose(fp);
    return count;
}

static int find_by_id(int id, Enquiry *out, int *slot_idx) {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) return 0;
    if (!read_header(fp)) { fclose(fp); return 0; }
    Enquiry tmp;
    int i;
    for (i = 0; i < g_total_records; i++) {
        if (fread(&tmp, sizeof(Enquiry), 1, fp) == 1) {
            if (tmp.id == id && !tmp.is_deleted) {
                *out      = tmp;
                *slot_idx = i;
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

/* ══════════════════════════════════════════════════════════
   DISPLAY HELPERS
   ══════════════════════════════════════════════════════════ */

static void print_banner(void) {
    printf("\n%s%s", COL_BOLD, COL_CYAN);
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║      Tour Enquiry CLI Manager                        ║\n");
    printf("║      Persistent CRUD  |  Binary File Storage  (C)   ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("%s\n", COL_RESET);
}

static void print_separator(void) {
    printf("%s──────────────────────────────────────────────────────%s\n",
           COL_CYAN, COL_RESET);
}

static void print_enquiry_summary(const Enquiry *e) {
    printf("  %s[#%04d]%s %-28s  %s->%s %-24s  "
           "%2d pax  %s%-11s%s  %s\n",
           COL_YELLOW, e->id, COL_RESET,
           e->name,
           COL_BLUE, COL_RESET,
           e->destination,
           e->num_travelers,
           status_color(e->status), status_label(e->status), COL_RESET,
           e->travel_date);
}

static void print_enquiry_detail(const Enquiry *e) {
    printf("\n");
    print_separator();
    printf("  %s%sEnquiry #%04d%s\n", COL_BOLD, COL_YELLOW, e->id, COL_RESET);
    print_separator();
    printf("  %sCustomer     :%s %s\n",  COL_CYAN, COL_RESET, e->name);
    printf("  %sPhone        :%s %s\n",  COL_CYAN, COL_RESET, e->phone);
    printf("  %sEmail        :%s %s\n",  COL_CYAN, COL_RESET, e->email);
    printf("  %sDestination  :%s %s\n",  COL_CYAN, COL_RESET, e->destination);
    printf("  %sTravel Date  :%s %s\n",  COL_CYAN, COL_RESET, e->travel_date);
    printf("  %sReturn Date  :%s %s\n",  COL_CYAN, COL_RESET, e->return_date);
    printf("  %sTravelers    :%s %d\n",  COL_CYAN, COL_RESET, e->num_travelers);
    printf("  %sBudget       :%s %s\n",  COL_CYAN, COL_RESET, budget_label(e->budget));
    printf("  %sStatus       :%s %s%s%s\n",
           COL_CYAN, COL_RESET,
           status_color(e->status), status_label(e->status), COL_RESET);
    printf("  %sNotes        :%s %s\n",  COL_CYAN, COL_RESET,
           e->notes[0] ? e->notes : "(none)");
    printf("  %sCreated On   :%s %s\n",  COL_CYAN, COL_RESET, e->created_on);
    print_separator();
}

/* ══════════════════════════════════════════════════════════
   CRUD OPERATIONS
   ══════════════════════════════════════════════════════════ */

/* ── CREATE ─────────────────────────────────────────────── */
static void create_enquiry(void) {
    Enquiry e;
    memset(&e, 0, sizeof(e));

    printf("\n%s%s── New Tour Enquiry ──────────────────────────────────%s\n",
           COL_BOLD, COL_GREEN, COL_RESET);

    do {
        input_string("  Customer Name    : ", e.name, MAX_NAME);
        if (!e.name[0])
            printf("%s  Name cannot be empty.%s\n", COL_RED, COL_RESET);
    } while (!e.name[0]);

    input_string("  Phone Number     : ", e.phone, MAX_PHONE);
    input_string("  Email Address    : ", e.email,  MAX_EMAIL);

    do {
        input_string("  Destination      : ", e.destination, MAX_DESTINATION);
        if (!e.destination[0])
            printf("%s  Destination cannot be empty.%s\n", COL_RED, COL_RESET);
    } while (!e.destination[0]);

    do {
        input_string("  Travel Date (DD/MM/YYYY) : ", e.travel_date, MAX_DATE);
        if (!valid_date(e.travel_date))
            printf("%s  Invalid date — use DD/MM/YYYY, year >= 2024.%s\n",
                   COL_RED, COL_RESET);
    } while (!valid_date(e.travel_date));

    do {
        input_string("  Return Date (DD/MM/YYYY) : ", e.return_date, MAX_DATE);
        if (!valid_date(e.return_date))
            printf("%s  Invalid date — use DD/MM/YYYY, year >= 2024.%s\n",
                   COL_RED, COL_RESET);
    } while (!valid_date(e.return_date));

    e.num_travelers = input_int("  Number of Travelers (1-50) : ", 1, 50);

    printf("\n  Budget Category:\n");
    printf("    %s0%s Economy   %s1%s Standard   %s2%s Premium   %s3%s Luxury\n",
           COL_YELLOW, COL_RESET, COL_YELLOW, COL_RESET,
           COL_YELLOW, COL_RESET, COL_YELLOW, COL_RESET);
    e.budget = (BudgetCategory)input_int("  Choose (0-3) : ", 0, 3);

    input_string("  Additional Notes : ", e.notes, MAX_NOTES);

    e.id         = g_next_id;
    e.status     = STATUS_NEW;
    e.is_deleted = 0;
    today_string(e.created_on);

    if (save_new_enquiry(&e))
        printf("\n%s  OK  Enquiry #%04d saved successfully!%s\n\n",
               COL_GREEN, e.id, COL_RESET);
    else
        printf("%s  FAIL  Could not save enquiry.%s\n\n", COL_RED, COL_RESET);
}

/* ── LIST ALL ────────────────────────────────────────────── */
static void list_enquiries(void) {
    Enquiry arr[MAX_ENQUIRIES];
    int count = load_all_enquiries(arr, MAX_ENQUIRIES);
    int i;

    printf("\n%s%s── All Enquiries (%d records) ────────────────────────%s\n",
           COL_BOLD, COL_CYAN, count, COL_RESET);

    if (count == 0) {
        printf("  %sNo enquiries found.%s\n\n", COL_YELLOW, COL_RESET);
        return;
    }

    printf("\n  %s%-8s %-28s %-24s %-4s %-12s %-10s%s\n",
           COL_BOLD, "ID", "Customer", "Destination",
           "Pax", "Status", "Travel Date", COL_RESET);
    print_separator();

    for (i = 0; i < count; i++)
        print_enquiry_summary(&arr[i]);

    printf("\n");
}

/* ── VIEW SINGLE ─────────────────────────────────────────── */
static void view_enquiry(void) {
    int id = input_int("\n  Enter Enquiry ID to view : ", 1, 999999);
    Enquiry e;
    int slot;
    if (find_by_id(id, &e, &slot))
        print_enquiry_detail(&e);
    else
        printf("%s  Enquiry #%04d not found.%s\n\n", COL_RED, id, COL_RESET);
}

/* ── UPDATE ──────────────────────────────────────────────── */

/* Update a single text field interactively (keep on empty input) */
static void update_field(const char *label, char *field, int field_size) {
    char tmp[MAX_TMP];
    printf("  %s [%s] : ", label, field);
    fflush(stdout);
    safe_fgets(tmp, (int)sizeof(tmp));
    if (tmp[0] != '\0') {
        /* copy at most field_size-1 chars, always terminate */
        int n = field_size - 1;
        int i;
        for (i = 0; i < n && tmp[i]; i++)
            field[i] = tmp[i];
        field[i] = '\0';
    }
}

static void update_enquiry(void) {
    int id = input_int("\n  Enter Enquiry ID to update : ", 1, 999999);
    Enquiry e;
    int slot;
    if (!find_by_id(id, &e, &slot)) {
        printf("%s  Enquiry #%04d not found.%s\n\n", COL_RED, id, COL_RESET);
        return;
    }

    print_enquiry_detail(&e);
    printf("%s%s── Update Fields (press Enter to keep current value) ─%s\n\n",
           COL_BOLD, COL_YELLOW, COL_RESET);

    update_field("Customer Name   ", e.name,        MAX_NAME);
    update_field("Phone Number    ", e.phone,        MAX_PHONE);
    update_field("Email Address   ", e.email,        MAX_EMAIL);
    update_field("Destination     ", e.destination,  MAX_DESTINATION);

    /* Travel date with validation */
    char tmp[MAX_TMP];
    do {
        printf("  Travel Date (DD/MM/YYYY) [%s] : ", e.travel_date);
        fflush(stdout);
        safe_fgets(tmp, (int)sizeof(tmp));
        if (tmp[0] == '\0') break;
        if (valid_date(tmp)) { safe_copy(e.travel_date, tmp, MAX_DATE); break; }
        printf("%s  Invalid date format.%s\n", COL_RED, COL_RESET);
    } while (1);

    do {
        printf("  Return Date (DD/MM/YYYY) [%s] : ", e.return_date);
        fflush(stdout);
        safe_fgets(tmp, (int)sizeof(tmp));
        if (tmp[0] == '\0') break;
        if (valid_date(tmp)) { safe_copy(e.return_date, tmp, MAX_DATE); break; }
        printf("%s  Invalid date format.%s\n", COL_RED, COL_RESET);
    } while (1);

    /* Travelers */
    printf("  Number of Travelers [%d] (0 = keep) : ", e.num_travelers);
    fflush(stdout);
    safe_fgets(tmp, (int)sizeof(tmp));
    int t = 0;
    if (sscanf(tmp, "%d", &t) == 1 && t >= 1 && t <= 50)
        e.num_travelers = t;

    /* Budget */
    printf("\n  Budget [%s]  0=Economy 1=Standard 2=Premium 3=Luxury\n",
           budget_label(e.budget));
    printf("  New choice (-1 = keep) : ");
    fflush(stdout);
    safe_fgets(tmp, (int)sizeof(tmp));
    int b = -1;
    if (sscanf(tmp, "%d", &b) == 1 && b >= 0 && b <= 3)
        e.budget = (BudgetCategory)b;

    /* Status */
    printf("\n  Status [%s]  0=New 1=Contacted 2=Confirmed 3=Cancelled 4=Completed\n",
           status_label(e.status));
    printf("  New choice (-1 = keep) : ");
    fflush(stdout);
    safe_fgets(tmp, (int)sizeof(tmp));
    int st = -1;
    if (sscanf(tmp, "%d", &st) == 1 && st >= 0 && st <= 4)
        e.status = (EnquiryStatus)st;

    update_field("Notes           ", e.notes, MAX_NOTES);

    if (update_record_at(slot, &e))
        printf("\n%s  OK  Enquiry #%04d updated successfully!%s\n\n",
               COL_GREEN, id, COL_RESET);
    else
        printf("%s  FAIL  Could not update enquiry.%s\n\n", COL_RED, COL_RESET);
}

/* ── DELETE (soft) ───────────────────────────────────────── */
static void delete_enquiry(void) {
    int id = input_int("\n  Enter Enquiry ID to delete : ", 1, 999999);
    Enquiry e;
    int slot;
    if (!find_by_id(id, &e, &slot)) {
        printf("%s  Enquiry #%04d not found.%s\n\n", COL_RED, id, COL_RESET);
        return;
    }

    print_enquiry_detail(&e);
    printf("%s  WARNING: This will permanently remove enquiry #%04d.%s\n",
           COL_RED, id, COL_RESET);

    char confirm[8];
    input_string("  Type YES to confirm : ", confirm, (int)sizeof(confirm));

    if (strcmp(confirm, "YES") != 0) {
        printf("  Deletion cancelled.\n\n");
        return;
    }

    e.is_deleted = 1;
    if (update_record_at(slot, &e))
        printf("\n%s  OK  Enquiry #%04d deleted.%s\n\n", COL_GREEN, id, COL_RESET);
    else
        printf("%s  FAIL  Could not delete enquiry.%s\n\n", COL_RED, COL_RESET);
}

/* ── SEARCH ──────────────────────────────────────────────── */
static void search_enquiries(void) {
    printf("\n%s%s── Search Enquiries ──────────────────────────────────%s\n",
           COL_BOLD, COL_MAGENTA, COL_RESET);
    printf("  Search by:  %s1%s Name   %s2%s Destination   %s3%s Status\n\n",
           COL_YELLOW, COL_RESET, COL_YELLOW, COL_RESET,
           COL_YELLOW, COL_RESET);
    int choice = input_int("  Choice : ", 1, 3);

    char keyword[MAX_DESTINATION];
    int  status_filter = -1;

    keyword[0] = '\0';
    if (choice == 3) {
        printf("  Status: 0=New 1=Contacted 2=Confirmed 3=Cancelled 4=Completed\n");
        status_filter = input_int("  Status number : ", 0, 4);
    } else {
        input_string("  Enter keyword : ", keyword, (int)sizeof(keyword));
        str_tolower(keyword);
    }

    Enquiry arr[MAX_ENQUIRIES];
    int total = load_all_enquiries(arr, MAX_ENQUIRIES);
    int found = 0;
    int i;

    printf("\n  %s%-8s %-28s %-24s %-4s %-12s %-10s%s\n",
           COL_BOLD, "ID", "Customer", "Destination",
           "Pax", "Status", "Travel Date", COL_RESET);
    print_separator();

    for (i = 0; i < total; i++) {
        int match = 0;
        if (choice == 1) {
            char lc[MAX_NAME];
            safe_copy(lc, arr[i].name, MAX_NAME);
            str_tolower(lc);
            match = (strstr(lc, keyword) != NULL);
        } else if (choice == 2) {
            char lc[MAX_DESTINATION];
            safe_copy(lc, arr[i].destination, MAX_DESTINATION);
            str_tolower(lc);
            match = (strstr(lc, keyword) != NULL);
        } else {
            match = (arr[i].status == (EnquiryStatus)status_filter);
        }
        if (match) {
            print_enquiry_summary(&arr[i]);
            found++;
        }
    }

    if (found == 0)
        printf("  %sNo matching enquiries found.%s\n", COL_YELLOW, COL_RESET);
    else
        printf("\n  %s%d result(s) found.%s\n", COL_GREEN, found, COL_RESET);
    printf("\n");
}

/* ── STATISTICS DASHBOARD ────────────────────────────────── */
static void show_statistics(void) {
    Enquiry arr[MAX_ENQUIRIES];
    int total = load_all_enquiries(arr, MAX_ENQUIRIES);
    int status_count[5] = {0};
    int budget_count[4] = {0};
    int total_travelers = 0;
    int i;

    for (i = 0; i < total; i++) {
        if (arr[i].status >= 0 && arr[i].status < 5)
            status_count[(int)arr[i].status]++;
        if (arr[i].budget >= 0 && arr[i].budget < 4)
            budget_count[(int)arr[i].budget]++;
        total_travelers += arr[i].num_travelers;
    }

    printf("\n%s%s── Statistics Dashboard ─────────────────────────────%s\n\n",
           COL_BOLD, COL_BLUE, COL_RESET);
    printf("  %sTotal active enquiries:%s %d\n", COL_BOLD, COL_RESET, total);
    printf("  %sTotal travelers:       %s %d\n\n", COL_BOLD, COL_RESET, total_travelers);

    const char *snames[5] = {"New","Contacted","Confirmed","Cancelled","Completed"};
    printf("  %sBy Status:%s\n", COL_BOLD, COL_RESET);
    for (i = 0; i < 5; i++) {
        int bars = (total > 0) ? (status_count[i] * 20 / total) : 0;
        int b;
        printf("  %s%-12s%s [", status_color((EnquiryStatus)i), snames[i], COL_RESET);
        for (b = 0; b < 20; b++) printf("%s", b < bars ? "#" : ".");
        printf("] %d\n", status_count[i]);
    }

    const char *bnames[4] = {"Economy","Standard","Premium","Luxury"};
    printf("\n  %sBy Budget:%s\n", COL_BOLD, COL_RESET);
    for (i = 0; i < 4; i++) {
        int bars = (total > 0) ? (budget_count[i] * 20 / total) : 0;
        int b;
        printf("  %-12s [", bnames[i]);
        for (b = 0; b < 20; b++) printf("%s", b < bars ? "#" : ".");
        printf("] %d\n", budget_count[i]);
    }
    printf("\n");
}

/* ── EXPORT TO CSV ───────────────────────────────────────── */
static void export_csv(void) {
    Enquiry arr[MAX_ENQUIRIES];
    int total = load_all_enquiries(arr, MAX_ENQUIRIES);
    int i;

    if (total == 0) {
        printf("  %sNo records to export.%s\n\n", COL_YELLOW, COL_RESET);
        return;
    }

    FILE *fp = fopen("enquiries_export.csv", "w");
    if (!fp) {
        printf("%s  Cannot create CSV file.%s\n\n", COL_RED, COL_RESET);
        return;
    }

    fprintf(fp, "ID,Name,Phone,Email,Destination,Travel Date,Return Date,"
                "Travelers,Budget,Status,Notes,Created On\n");

    for (i = 0; i < total; i++) {
        Enquiry *e = &arr[i];
        fprintf(fp, "%d,\"%s\",\"%s\",\"%s\",\"%s\",%s,%s,%d,%s,%s,\"%s\",%s\n",
                e->id, e->name, e->phone, e->email, e->destination,
                e->travel_date, e->return_date, e->num_travelers,
                budget_label(e->budget), status_label(e->status),
                e->notes, e->created_on);
    }

    fclose(fp);
    printf("%s  OK  Exported %d records to enquiries_export.csv%s\n\n",
           COL_GREEN, total, COL_RESET);
}

/* ── QUICK STATUS UPDATE ─────────────────────────────────── */
static void quick_status_update(void) {
    int id = input_int("\n  Enter Enquiry ID : ", 1, 999999);
    Enquiry e;
    int slot;
    if (!find_by_id(id, &e, &slot)) {
        printf("%s  Enquiry #%04d not found.%s\n\n", COL_RED, id, COL_RESET);
        return;
    }

    printf("  Current status: %s%s%s\n\n",
           status_color(e.status), status_label(e.status), COL_RESET);
    printf("  0=New  1=Contacted  2=Confirmed  3=Cancelled  4=Completed\n");
    int ns = input_int("  New status : ", 0, 4);
    e.status = (EnquiryStatus)ns;

    if (update_record_at(slot, &e))
        printf("\n%s  OK  Status updated to \"%s\"%s\n\n",
               COL_GREEN, status_label(e.status), COL_RESET);
    else
        printf("%s  FAIL  Could not update.%s\n\n", COL_RED, COL_RESET);
}

/* ══════════════════════════════════════════════════════════
   MAIN MENU
   ══════════════════════════════════════════════════════════ */

static void print_menu(void) {
    printf("%s%s", COL_BOLD, COL_WHITE);
    printf("  +-------------------------------------+\n");
    printf("  |           MAIN MENU                 |\n");
    printf("  +-------------------------------------+\n");
    printf("  |  %s1%s  Add New Enquiry                  |\n", COL_GREEN,  COL_WHITE);
    printf("  |  %s2%s  List All Enquiries               |\n", COL_GREEN,  COL_WHITE);
    printf("  |  %s3%s  View Enquiry Details             |\n", COL_CYAN,   COL_WHITE);
    printf("  |  %s4%s  Update Enquiry                   |\n", COL_YELLOW, COL_WHITE);
    printf("  |  %s5%s  Delete Enquiry                   |\n", COL_RED,    COL_WHITE);
    printf("  |  %s6%s  Search Enquiries                 |\n", COL_MAGENTA,COL_WHITE);
    printf("  |  %s7%s  Quick Status Update              |\n", COL_CYAN,   COL_WHITE);
    printf("  |  %s8%s  Statistics Dashboard             |\n", COL_BLUE,   COL_WHITE);
    printf("  |  %s9%s  Export to CSV                    |\n", COL_GREEN,  COL_WHITE);
    printf("  |  %s0%s  Exit                             |\n", COL_RED,    COL_WHITE);
    printf("  +-------------------------------------+\n");
    printf("%s\n", COL_RESET);
}

int main(void) {
    if (!ensure_data_file()) {
        fprintf(stderr, "Fatal: cannot initialise data file.\n");
        return EXIT_FAILURE;
    }

    print_banner();
    printf("  Data file : %s%s%s   Active slots : %s%d%s\n\n",
           COL_CYAN, DATA_FILE, COL_RESET,
           COL_GREEN, g_total_records, COL_RESET);

    int running = 1;
    while (running) {
        print_menu();
        int choice = input_int("  Select option : ", 0, 9);
        switch (choice) {
            case 1: create_enquiry();      break;
            case 2: list_enquiries();      break;
            case 3: view_enquiry();        break;
            case 4: update_enquiry();      break;
            case 5: delete_enquiry();      break;
            case 6: search_enquiries();    break;
            case 7: quick_status_update(); break;
            case 8: show_statistics();     break;
            case 9: export_csv();          break;
            case 0:
                printf("\n%s  Goodbye! All data saved to %s%s\n\n",
                       COL_GREEN, DATA_FILE, COL_RESET);
                running = 0;
                break;
        }
    }
    return EXIT_SUCCESS;
}
