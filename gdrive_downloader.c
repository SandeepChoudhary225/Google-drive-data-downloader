/*
 * gdrive_downloader.c
 * ====================
 * Downloads files and folders from Google Drive (public share links).
 * Uses Google Drive API v3 for reliable folder listing.
 * Uses curl (built-in on Windows 10/11) for downloads.
 * Read-only: only GET requests are sent.
 *
 * Setup:  Get a free Google API key from https://console.cloud.google.com
 *         (APIs & Services > Credentials > Create > API Key)
 *         Enable "Google Drive API" in the API Library.
 *
 * Compile:  gcc gdrive_downloader.c -o gdrive_downloader.exe -lshell32
 * Run:      gdrive_downloader.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <shellapi.h>
#include <direct.h>

/* ═══════════════════════════════════════════════════════════════════ */
/*  Constants                                                        */
/* ═══════════════════════════════════════════════════════════════════ */

#define CLR_RESET   7
#define CLR_CYAN    11
#define CLR_WHITE   15
#define CLR_GREY    8
#define CLR_YELLOW  14
#define CLR_PINK    13
#define CLR_RED     12
#define CLR_GREEN   10

#define MAX_FILES 1000
#define CONFIG_FILE "gdrive_api_key.txt"

/* ═══════════════════════════════════════════════════════════════════ */
/*  Types                                                            */
/* ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    char id[128];
    char name[512];
    char mime[128];
    unsigned long long size;
} DriveFile;

/* ═══════════════════════════════════════════════════════════════════ */
/*  Globals                                                          */
/* ═══════════════════════════════════════════════════════════════════ */

static void  *g_console;
static char   g_exe_dir[1024];
static char   g_api_key[256];
static char   g_access_token[2048];
static char   g_client_id[256];
static char   g_client_secret[128];

/* ═══════════════════════════════════════════════════════════════════ */
/*  Console                                                          */
/* ═══════════════════════════════════════════════════════════════════ */

static void color(int c)
{
    SetConsoleTextAttribute(g_console, (unsigned short)c);
}

static void banner(void)
{
    color(CLR_YELLOW);
    printf("\n");
    printf("  +====================================================+\n");
    printf("  |        GOOGLE DRIVE DOWNLOADER  (API + curl)        |\n");
    printf("  |        Supports: Files & Folders                    |\n");
    printf("  +====================================================+\n");
    color(CLR_RESET);
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  String / File Helpers                                            */
/* ═══════════════════════════════════════════════════════════════════ */

static void trim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

static void clean_filename(char *name)
{
    for (int i = 0; name[i]; i++) {
        if (strchr("<>:\"/\\|?*", name[i]) || (unsigned char)name[i] < 32)
            name[i] = '_';
    }
}

static void format_size(unsigned long long bytes, char *out, int outlen)
{
    if (bytes < 1024ULL)
        snprintf(out, outlen, "%llu B", bytes);
    else if (bytes < 1024ULL * 1024)
        snprintf(out, outlen, "%.2f KB", bytes / 1024.0);
    else if (bytes < 1024ULL * 1024 * 1024)
        snprintf(out, outlen, "%.2f MB", bytes / (1024.0 * 1024));
    else if (bytes < 1024ULL * 1024 * 1024 * 1024)
        snprintf(out, outlen, "%.2f GB", bytes / (1024.0 * 1024 * 1024));
    else
        snprintf(out, outlen, "%.2f TB", bytes / (1024.0 * 1024 * 1024 * 1024));
}

static void init_exe_dir(void)
{
    GetModuleFileNameA(NULL, g_exe_dir, sizeof(g_exe_dir));
    char *p = strrchr(g_exe_dir, '\\');
    if (p) *(p + 1) = '\0';
}

static unsigned long long file_size_on_disk(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

static int file_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static int ask_yn(const char *prompt)
{
    char buf[16];
    color(CLR_YELLOW);
    printf("  %s (y/n): ", prompt);
    color(CLR_WHITE);
    if (fgets(buf, sizeof(buf), stdin) == NULL) return 0;
    trim(buf);
    return (buf[0] == 'y' || buf[0] == 'Y');
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  API Key Management                                               */
/* ═══════════════════════════════════════════════════════════════════ */

static int load_api_key(void)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", g_exe_dir, CONFIG_FILE);

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    if (fgets(g_api_key, sizeof(g_api_key), f) != NULL) {
        trim(g_api_key);
    }
    fclose(f);
    return (strlen(g_api_key) > 10);
}

static void save_api_key(void)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", g_exe_dir, CONFIG_FILE);

    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", g_api_key);
        fclose(f);
    }
}

static int setup_api_key(void)
{
    color(CLR_YELLOW);
    printf("  ══════════════════════════════════════════════════\n");
    printf("  FIRST-TIME SETUP: Google API Key\n");
    printf("  ══════════════════════════════════════════════════\n\n");
    color(CLR_GREY);
    printf("  To list folder contents, this program needs a\n");
    printf("  free Google API key. Here's how to get one:\n\n");
    color(CLR_WHITE);
    printf("  1. Go to: https://console.cloud.google.com\n");
    printf("  2. Create a project (or use existing)\n");
    printf("  3. Go to APIs & Services > Library\n");
    printf("  4. Search 'Google Drive API' and ENABLE it\n");
    printf("  5. Go to APIs & Services > Credentials\n");
    printf("  6. Click 'Create Credentials' > 'API Key'\n");
    printf("  7. Copy the key and paste it below\n\n");
    color(CLR_GREY);
    printf("  (The key is saved locally in %s)\n", CONFIG_FILE);
    printf("  (Single files work without a key)\n\n");

    color(CLR_YELLOW);
    printf("  Paste your API key (or press Enter to skip): ");
    color(CLR_WHITE);

    char input[256];
    if (fgets(input, sizeof(input), stdin) == NULL) return 0;
    trim(input);

    if (strlen(input) < 10) {
        color(CLR_GREY);
        printf("  Skipped. Folder downloads won't work.\n\n");
        return 0;
    }

    strncpy(g_api_key, input, sizeof(g_api_key) - 1);
    g_api_key[sizeof(g_api_key) - 1] = '\0';
    save_api_key();

    color(CLR_GREEN);
    printf("  API key saved!\n\n");
    return 1;
}

/* Forward declaration */
static const char *json_get_string(const char *json, const char *key,
                                    char *out, int outlen);

/* ═══════════════════════════════════════════════════════════════════ */
/*  OAuth2 Login (for downloading private/large files)               */
/* ═══════════════════════════════════════════════════════════════════ */

#define TOKEN_FILE    "gdrive_token.txt"
#define CLIENT_FILE   "gdrive_client.txt"

static int load_token(void)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", g_exe_dir, TOKEN_FILE);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (fgets(g_access_token, sizeof(g_access_token), f) != NULL)
        trim(g_access_token);
    fclose(f);
    return (strlen(g_access_token) > 10);
}

static void save_token(void)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", g_exe_dir, TOKEN_FILE);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%s\n", g_access_token); fclose(f); }
}

static int load_client_creds(void)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", g_exe_dir, CLIENT_FILE);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (fgets(g_client_id, sizeof(g_client_id), f) != NULL) trim(g_client_id);
    if (fgets(g_client_secret, sizeof(g_client_secret), f) != NULL) trim(g_client_secret);
    fclose(f);
    return (strlen(g_client_id) > 10 && strlen(g_client_secret) > 5);
}

static void save_client_creds(void)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", g_exe_dir, CLIENT_FILE);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n%s\n", g_client_id, g_client_secret);
        fclose(f);
    }
}

static int do_login(void)
{
    /* Step 1: Get client credentials if not saved */
    if (!load_client_creds()) {
        color(CLR_YELLOW);
        printf("  ══════════════════════════════════════════════════\n");
        printf("  GOOGLE LOGIN SETUP\n");
        printf("  ══════════════════════════════════════════════════\n\n");
        color(CLR_GREY);
        printf("  To download files, we need OAuth2 credentials:\n\n");
        color(CLR_WHITE);
        printf("  1. Go to: https://console.cloud.google.com\n");
        printf("  2. Select or create a Google Cloud project\n");
        printf("  3. APIs & Services > Library\n");
        printf("  4. Search for 'Google Drive API' and click ENABLE\n");
        printf("  5. APIs & Services > Credentials\n");
        printf("  6. Create Credentials > OAuth client ID\n");
        printf("  7. Application type: Desktop app\n");
        printf("  8. Copy the Client ID and Client Secret\n\n");

        color(CLR_YELLOW);
        printf("  Paste Client ID: ");
        color(CLR_WHITE);
        char input[256];
        if (fgets(input, sizeof(input), stdin) == NULL) return 0;
        trim(input);
        if (strlen(input) < 10) return 0;
        strncpy(g_client_id, input, sizeof(g_client_id) - 1);

        color(CLR_YELLOW);
        printf("  Paste Client Secret: ");
        color(CLR_WHITE);
        if (fgets(input, sizeof(input), stdin) == NULL) return 0;
        trim(input);
        if (strlen(input) < 5) return 0;
        strncpy(g_client_secret, input, sizeof(g_client_secret) - 1);

        save_client_creds();
        color(CLR_GREEN);
        printf("  Credentials saved!\n\n");
    }

    /* Step 2: Open browser for Google login */
    char auth_url[2048];
    snprintf(auth_url, sizeof(auth_url),
        "https://accounts.google.com/o/oauth2/auth"
        "?client_id=%s"
        "&redirect_uri=urn:ietf:wg:oauth:2.0:oob"
        "&scope=https://www.googleapis.com/auth/drive.readonly"
        "&response_type=code"
        "&access_type=offline",
        g_client_id);

    color(CLR_GREY);
    printf("  Opening browser for Google login...\n");
    ShellExecuteA(NULL, "open", auth_url, NULL, NULL, SW_SHOWNORMAL);

    printf("\n");
    color(CLR_YELLOW);
    printf("  After logging in, Google will show you a code.\n");
    printf("  Paste the authorization code here: ");
    color(CLR_WHITE);

    char auth_code[512];
    if (fgets(auth_code, sizeof(auth_code), stdin) == NULL) return 0;
    trim(auth_code);
    if (strlen(auth_code) < 5) return 0;

    /* Step 3: Exchange code for access token */
    color(CLR_GREY);
    printf("  Exchanging code for access token...\n");

    char tmpfile[2048];
    char cmd[8192];
    snprintf(tmpfile, sizeof(tmpfile), "%sgdrive_token_resp.json", g_exe_dir);

    snprintf(cmd, sizeof(cmd),
        "curl.exe -s -X POST "
        "-d \"code=%s"
        "&client_id=%s"
        "&client_secret=%s"
        "&redirect_uri=urn:ietf:wg:oauth:2.0:oob"
        "&grant_type=authorization_code\" "
        "\"https://oauth2.googleapis.com/token\" "
        "-o \"%s\" 2>NUL",
        auth_code, g_client_id, g_client_secret, tmpfile);

    system(cmd);

    /* Parse the response */
    FILE *f = fopen(tmpfile, "r");
    if (!f) { color(CLR_RED); printf("  [ERROR] No response.\n"); return 0; }

    char resp[4096] = {0};
    fread(resp, 1, sizeof(resp) - 1, f);
    fclose(f);
    remove(tmpfile);

    /* Extract access_token */
    char token[2048] = {0};
    json_get_string(resp, "access_token", token, sizeof(token));

    if (strlen(token) < 10) {
        color(CLR_RED);
        char err[256] = {0};
        json_get_string(resp, "error_description", err, sizeof(err));
        printf("  [ERROR] Login failed: %s\n", err[0] ? err : "unknown error");
        return 0;
    }

    strncpy(g_access_token, token, sizeof(g_access_token) - 1);
    save_token();

    color(CLR_GREEN);
    printf("  Login successful! Token saved.\n\n");
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Link Parsing                                                     */
/* ═══════════════════════════════════════════════════════════════════ */

#define LINK_NONE   0
#define LINK_FILE   1
#define LINK_FOLDER 2

static int parse_link(const char *link, char *id_out, int id_size)
{
    const char *p;
    int i;

    p = strstr(link, "/folders/");
    if (p) {
        p += 9;
        i = 0;
        while (*p && *p != '/' && *p != '?' && *p != '#' && i < id_size - 1)
            id_out[i++] = *p++;
        id_out[i] = '\0';
        return (i > 5) ? LINK_FOLDER : LINK_NONE;
    }

    p = strstr(link, "/file/d/");
    if (p) {
        p += 8;
        i = 0;
        while (*p && *p != '/' && *p != '?' && i < id_size - 1)
            id_out[i++] = *p++;
        id_out[i] = '\0';
        return (i > 5) ? LINK_FILE : LINK_NONE;
    }

    p = strstr(link, "id=");
    if (p) {
        p += 3;
        i = 0;
        while (*p && *p != '&' && *p != '#' && *p != ' ' && i < id_size - 1)
            id_out[i++] = *p++;
        id_out[i] = '\0';
        return (i > 5) ? LINK_FILE : LINK_NONE;
    }

    int len = (int)strlen(link);
    if (len > 10 && len < 100) {
        int ok = 1;
        for (i = 0; i < len; i++) {
            if (!isalnum((unsigned char)link[i]) && link[i] != '-' && link[i] != '_')
                { ok = 0; break; }
        }
        if (ok) {
            strncpy(id_out, link, id_size - 1);
            id_out[id_size - 1] = '\0';
            return LINK_FILE;
        }
    }

    return LINK_NONE;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  curl Helpers                                                     */
/* ═══════════════════════════════════════════════════════════════════ */

/*
 * Run curl.exe, save output to a temp file, read into malloc'd buffer.
 * Returns NULL on failure. Caller must free().
 */
static char *curl_to_buffer(const char *curl_args)
{
    char tmpfile[2048];
    char cmd[8192];

    snprintf(tmpfile, sizeof(tmpfile), "%sgdrive_tmp.dat", g_exe_dir);
    snprintf(cmd, sizeof(cmd), "curl.exe %s -o \"%s\" 2>NUL", curl_args, tmpfile);

    system(cmd);

    FILE *f = fopen(tmpfile, "rb");
    if (!f) { remove(tmpfile); return NULL; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        remove(tmpfile);
        return NULL;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); remove(tmpfile); return NULL; }

    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    remove(tmpfile);
    return buf;
}
/*
 * Download a Google Drive file by ID using curl.
 * Tries 3 methods in order:
 *   1. Google Drive API v3  (if API key available)
 *   2. drive.google.com/uc  (with cookie jar for large files)
 *   3. drive.usercontent.google.com (fallback)
 *
 * Returns 1 on success, 0 on failure.
 */
static int verify_download(const char *save_path)
{
    if (!file_exists(save_path)) return 0;

    unsigned long long sz = file_size_on_disk(save_path);
    if (sz == 0) { remove(save_path); return 0; }

    /* Detect HTML error/login page */
    if (sz < 100000) {
        FILE *f = fopen(save_path, "rb");
        if (f) {
            char peek[1024] = {0};
            size_t n = fread(peek, 1, sizeof(peek) - 1, f);
            fclose(f);
            peek[n] = '\0';
            if ((strstr(peek, "<!DOCTYPE") || strstr(peek, "<html")) &&
                (strstr(peek, "Google") || strstr(peek, "ServiceLogin") ||
                 strstr(peek, "confirm") || strstr(peek, "virus")))
            {
                remove(save_path);
                return 0;
            }
        }
    }

    return 1;
}

static int download_by_id(const char *file_id, const char *save_path)
{
    char cmd[8192];
    char cookie_file[2048];

    printf("\n");

    /* ── Method 1: OAuth2 token (most reliable — works for everything) ── */
    if (g_access_token[0]) {
        color(CLR_GREY);
        printf("  [Method 1: OAuth2 token]\n");
        color(CLR_RESET);

        snprintf(cmd, sizeof(cmd),
            "curl.exe -L -# "
            "-H \"Authorization: Bearer %s\" "
            "-o \"%s\" "
            "\"https://www.googleapis.com/drive/v3/files/%s?alt=media\"",
            g_access_token, save_path, file_id);

        system(cmd);
        if (verify_download(save_path)) return 1;

        /* Token might be expired */
        color(CLR_GREY);
        printf("  Token may be expired. Trying other methods...\n");
    }

    /* ── Method 2: API key ── */
    if (g_api_key[0]) {
        color(CLR_GREY);
        printf("  [Method 2: API key]\n");
        color(CLR_RESET);

        snprintf(cmd, sizeof(cmd),
            "curl.exe -L -# "
            "-o \"%s\" "
            "\"https://www.googleapis.com/drive/v3/files/%s"
            "?alt=media&key=%s\"",
            save_path, file_id, g_api_key);

        system(cmd);
        if (verify_download(save_path)) return 1;
    }

    /* ── Method 3: drive.google.com/uc + cookie jar ── */
    color(CLR_GREY);
    printf("  [Method 2: uc + cookies]\n");
    color(CLR_RESET);

    snprintf(cookie_file, sizeof(cookie_file), "%sgdrive_dl_cookies.txt", g_exe_dir);

    snprintf(cmd, sizeof(cmd),
        "curl.exe -L -# "
        "-c \"%s\" -b \"%s\" "
        "-o \"%s\" "
        "\"https://drive.google.com/uc?export=download&confirm=t&id=%s\"",
        cookie_file, cookie_file, save_path, file_id);

    system(cmd);
    remove(cookie_file);
    if (verify_download(save_path)) return 1;

    /* ── Method 4: drive.usercontent.google.com ── */
    color(CLR_GREY);
    printf("  [Method 3: usercontent]\n");
    color(CLR_RESET);

    snprintf(cmd, sizeof(cmd),
        "curl.exe -L -# "
        "-A \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\" "
        "-o \"%s\" "
        "\"https://drive.usercontent.google.com/download"
        "?id=%s&export=download&confirm=t\"",
        save_path, file_id);

    system(cmd);
    return verify_download(save_path);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Google Drive API v3  (folder listing)                            */
/* ═══════════════════════════════════════════════════════════════════ */

/*
 * Simple JSON string value extractor.
 * Finds "key": "value" and copies value into out.
 * Returns pointer past the value, or NULL if not found.
 */
static const char *json_get_string(const char *json, const char *key,
                                    char *out, int outlen)
{
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return NULL;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p != '"') return NULL;
    p++;

    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        if (*p == '\\' && p[1]) { p++; }  /* skip escape */
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/*
 * List files in a Google Drive folder using the API.
 * Returns the number of files found.
 */
static int list_folder_api(const char *folder_id, DriveFile *files, int max)
{
    if (g_api_key[0] == '\0') return -1;  /* no API key */

    char args[2048];
    snprintf(args, sizeof(args),
        "-sL \"https://www.googleapis.com/drive/v3/files"
        "?q=%%27%s%%27+in+parents"
        "&key=%s"
        "&fields=files(id,name,size,mimeType)"
        "&pageSize=1000"
        "&supportsAllDrives=true"
        "&includeItemsFromAllDrives=true\"",
        folder_id, g_api_key);

    char *json = curl_to_buffer(args);
    if (!json) return 0;

    /* Check for API error */
    if (strstr(json, "\"error\"")) {
        char msg[256] = {0};
        json_get_string(json, "message", msg, sizeof(msg));
        color(CLR_RED);
        printf("  [API ERROR] %s\n", msg[0] ? msg : "Unknown error");
        free(json);
        return 0;
    }

    /* Parse the JSON response */
    int count = 0;
    const char *p = json;

    while (count < max) {
        /* Find next file object */
        p = strstr(p, "\"id\"");
        if (!p) break;

        char id[128] = {0};
        char name[512] = {0};
        char mime[128] = {0};
        char size_str[32] = {0};

        /* Back up to find the start of this object */
        const char *obj = p;

        json_get_string(obj, "id", id, sizeof(id));
        json_get_string(obj, "name", name, sizeof(name));
        json_get_string(obj, "mimeType", mime, sizeof(mime));
        json_get_string(obj, "size", size_str, sizeof(size_str));

        p += 4;  /* move past this "id" to find the next one */

        if (id[0] == '\0') continue;

        /* Skip sub-folders (they have mimeType = application/vnd.google-apps.folder) */
        if (strstr(mime, "folder")) continue;

        strncpy(files[count].id, id, sizeof(files[count].id) - 1);
        files[count].id[sizeof(files[count].id) - 1] = '\0';
        strncpy(files[count].name, name, sizeof(files[count].name) - 1);
        files[count].name[sizeof(files[count].name) - 1] = '\0';
        strncpy(files[count].mime, mime, sizeof(files[count].mime) - 1);
        files[count].mime[sizeof(files[count].mime) - 1] = '\0';
        files[count].size = (size_str[0]) ? (unsigned long long)_atoi64(size_str) : 0;
        count++;
    }

    free(json);
    return count;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Folder Download                                                  */
/* ═══════════════════════════════════════════════════════════════════ */

static void handle_folder(const char *folder_id)
{
    color(CLR_GREY);
    printf("  Querying Google Drive API...\n");

    DriveFile *files = (DriveFile *)calloc(MAX_FILES, sizeof(DriveFile));
    if (!files) return;

    int count = list_folder_api(folder_id, files, MAX_FILES);

    if (count < 0) {
        color(CLR_RED);
        printf("  [ERROR] No API key configured. Folder listing requires one.\n");
        color(CLR_GREY);
        printf("  Run the program again to set up your API key.\n\n");
        free(files);
        return;
    }

    if (count == 0) {
        color(CLR_RED);
        printf("  No files found. Folder may be empty or API key invalid.\n\n");
        free(files);
        return;
    }

    /* Show file list */
    color(CLR_GREEN);
    printf("  Found %d file(s):\n\n", count);

    unsigned long long total_size = 0;
    for (int i = 0; i < count; i++) {
        clean_filename(files[i].name);
        char sz[32];
        format_size(files[i].size, sz, sizeof(sz));
        total_size += files[i].size;

        color(CLR_YELLOW);
        printf("  [%d] ", i + 1);
        color(CLR_WHITE);
        printf("%-40s ", files[i].name);
        color(CLR_PINK);
        printf("%s\n", sz);
    }

    char total_str[32];
    format_size(total_size, total_str, sizeof(total_str));
    color(CLR_GREY);
    printf("\n  Total: %s\n\n", total_str);

    if (!ask_yn("Download all files?")) {
        color(CLR_GREY);
        printf("  Cancelled.\n\n");
        free(files);
        return;
    }

    /* Create output folder */
    char out_dir[2048];
    snprintf(out_dir, sizeof(out_dir), "%sgdrive_%s", g_exe_dir, folder_id);
    _mkdir(out_dir);

    printf("\n");
    int ok_count = 0, fail_count = 0;

    for (int i = 0; i < count; i++) {
        color(CLR_YELLOW);
        printf("  ── [%d/%d] ────────────────────────────────────────\n", i + 1, count);
        color(CLR_WHITE);
        printf("  %s\n", files[i].name);

        char path[4096];
        snprintf(path, sizeof(path), "%s\\%s", out_dir, files[i].name);

        if (download_by_id(files[i].id, path)) {
            unsigned long long sz = file_size_on_disk(path);
            char sz_str[32];
            format_size(sz, sz_str, sizeof(sz_str));
            color(CLR_GREEN);
            printf("  OK (%s)\n\n", sz_str);
            ok_count++;
        } else {
            color(CLR_RED);
            printf("  FAILED\n\n");
            fail_count++;
        }
    }

    /* Summary */
    color(CLR_YELLOW);
    printf("  ══════════════════════════════════════════════════\n");
    printf("  DOWNLOAD COMPLETE\n");
    printf("  ══════════════════════════════════════════════════\n");
    color(CLR_GREEN);
    printf("  Success: %d\n", ok_count);
    if (fail_count > 0) {
        color(CLR_RED);
        printf("  Failed:  %d\n", fail_count);
    }
    color(CLR_GREY);
    printf("  Saved to: ");
    color(CLR_CYAN);
    printf("%s\n\n", out_dir);

    if (ask_yn("Open download folder?"))
        ShellExecuteA(NULL, "open", "explorer.exe", out_dir, NULL, SW_SHOWNORMAL);

    printf("\n");
    free(files);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Single File Download                                             */
/* ═══════════════════════════════════════════════════════════════════ */

static int fetch_filename_from_headers(const char *file_id, char *name, int name_size)
{
    char args[1024];
    snprintf(args, sizeof(args),
        "-sI -L -A \"Mozilla/5.0\" "
        "\"https://drive.usercontent.google.com/download"
        "?id=%s&export=download&confirm=t\"",
        file_id);

    char *headers = curl_to_buffer(args);
    if (!headers) return 0;

    int found = 0;
    char *line = strtok(headers, "\r\n");
    while (line) {
        if (strstr(line, "ontent-") && strstr(line, "isposition")) {
            /* Try filename*=UTF-8''encoded */
            char *q = strstr(line, "''");
            if (q) {
                q += 2;
                int i = 0;
                while (*q && *q != ';' && *q != '\r' && *q != '\n' && i < name_size - 1) {
                    if (*q == '%' && q[1] && q[2]) {
                        char hex[3] = { q[1], q[2], '\0' };
                        name[i++] = (char)strtol(hex, NULL, 16);
                        q += 3;
                    } else {
                        name[i++] = *q++;
                    }
                }
                name[i] = '\0';
                if (i > 0) found = 1;
            }
            /* Try filename="name" */
            if (!found) {
                q = strstr(line, "filename=\"");
                if (q) {
                    q += 10;
                    int i = 0;
                    while (*q && *q != '"' && i < name_size - 1)
                        name[i++] = *q++;
                    name[i] = '\0';
                    if (i > 0) found = 1;
                }
            }
            break;
        }
        line = strtok(NULL, "\r\n");
    }

    free(headers);
    return found;
}

static void handle_file(const char *file_id)
{
    color(CLR_GREY);
    printf("  Fetching file info...\n");

    char filename[512] = {0};

    /* Try API first (if key available) for accurate name + size */
    if (g_api_key[0]) {
        char args[1024];
        snprintf(args, sizeof(args),
            "-sL \"https://www.googleapis.com/drive/v3/files/%s"
            "?key=%s&fields=name,size,mimeType\"",
            file_id, g_api_key);

        char *json = curl_to_buffer(args);
        if (json) {
            json_get_string(json, "name", filename, sizeof(filename));
            free(json);
        }
    }

    /* Fallback: get name from HTTP headers */
    if (filename[0] == '\0') {
        fetch_filename_from_headers(file_id, filename, sizeof(filename));
    }

    if (filename[0] == '\0') {
        snprintf(filename, sizeof(filename), "%s_download", file_id);
    }
    clean_filename(filename);

    char save_path[2048];
    snprintf(save_path, sizeof(save_path), "%s%s", g_exe_dir, filename);

    color(CLR_GREY);
    printf("  File: ");
    color(CLR_WHITE);
    printf("%s\n", filename);
    color(CLR_GREY);
    printf("  Save: ");
    color(CLR_CYAN);
    printf("%s\n", save_path);

    if (file_exists(save_path)) {
        if (!ask_yn("File exists. Overwrite?")) {
            color(CLR_GREY);
            printf("  Skipped.\n\n");
            return;
        }
    }

    color(CLR_GREY);
    printf("\n  Downloading...");

    if (download_by_id(file_id, save_path)) {
        unsigned long long sz = file_size_on_disk(save_path);
        char sz_str[32];
        format_size(sz, sz_str, sizeof(sz_str));

        printf("\n");
        color(CLR_GREEN);
        printf("  Download complete!\n");
        color(CLR_GREY);
        printf("  File: ");
        color(CLR_WHITE);
        printf("%s\n", filename);
        color(CLR_GREY);
        printf("  Size: ");
        color(CLR_PINK);
        printf("%s\n", sz_str);
        color(CLR_GREY);
        printf("  Path: ");
        color(CLR_CYAN);
        printf("%s\n\n", save_path);

        if (ask_yn("Open file location?")) {
            char params[2100];
            snprintf(params, sizeof(params), "/select,\"%s\"", save_path);
            ShellExecuteA(NULL, "open", "explorer.exe", params, NULL, SW_SHOWNORMAL);
        }
    } else {
        printf("\n");
        color(CLR_RED);
        printf("  [ERROR] Download failed.\n");
        printf("  Ensure the file is shared publicly.\n");
    }
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Main                                                             */
/* ═══════════════════════════════════════════════════════════════════ */

int main(void)
{
    g_console = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(65001);
    init_exe_dir();
    banner();

    /* Check curl */
    if (system("curl.exe --version >NUL 2>NUL") != 0) {
        color(CLR_RED);
        printf("  [ERROR] curl not found. Built into Windows 10/11.\n");
        printf("  Press Enter to exit...");
        color(CLR_RESET);
        getchar();
        return 1;
    }

    /* Load or setup API key */
    if (!load_api_key()) {
        setup_api_key();
    } else {
        color(CLR_GREEN);
        printf("  API key loaded.\n");
    }

    /* Load saved OAuth2 token */
    if (load_token()) {
        color(CLR_GREEN);
        printf("  OAuth2 token loaded.\n");
    } else {
        color(CLR_GREY);
        printf("  No login token. Type 'login' to authenticate.\n");
    }
    printf("\n");

    /* Main loop */
    char input[2048];
    char id[128];

    while (1) {
        color(CLR_YELLOW);
        printf("  ──────────────────────────────────────────────────\n");
        printf("  Paste Google Drive link, or type: login / key / quit\n");
        printf("  ──────────────────────────────────────────────────\n");
        color(CLR_WHITE);
        printf("  > ");

        if (fgets(input, sizeof(input), stdin) == NULL) break;
        trim(input);

        if (input[0] == '\0') continue;
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) break;

        /* Commands */
        if (strcmp(input, "key") == 0 || strcmp(input, "apikey") == 0) {
            setup_api_key();
            continue;
        }
        if (strcmp(input, "login") == 0) {
            do_login();
            continue;
        }

        int type = parse_link(input, id, sizeof(id));

        if (type == LINK_NONE) {
            color(CLR_RED);
            printf("\n  Could not extract ID from link.\n");
            color(CLR_GREY);
            printf("  Supported:\n");
            printf("    drive.google.com/file/d/FILE_ID/view\n");
            printf("    drive.google.com/drive/folders/FOLDER_ID\n");
            printf("    drive.google.com/open?id=FILE_ID\n\n");
            continue;
        }

        color(CLR_GREY);
        printf("\n  %s ID: ", type == LINK_FOLDER ? "Folder" : "File");
        color(CLR_WHITE);
        printf("%s\n", id);

        if (type == LINK_FOLDER) {
            color(CLR_YELLOW);
            printf("  Mode: Folder (via Google Drive API)\n\n");
            handle_folder(id);
        } else {
            handle_file(id);
        }

        color(CLR_RESET);
    }

    color(CLR_GREY);
    printf("\n  Press Enter to exit...");
    color(CLR_RESET);
    getchar();
    printf("\n");
    return 0;
}
