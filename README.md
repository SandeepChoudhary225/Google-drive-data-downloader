# Google Drive Downloader (`gdrive_downloader.c`)

`gdrive_downloader.c` is a Windows console program for downloading files and
folders from Google Drive links. It uses `curl.exe` for downloads and the Google
Drive API v3 for reliable folder listing and file metadata.

The program is designed for read-only access. It sends download and metadata
requests, but it does not upload, edit, move, or delete files in Google Drive.

## Features

- Downloads single Google Drive files from shared links.
- Downloads all regular files inside a Google Drive folder.
- Uses a Google API key for folder listing and better filename detection.
- Supports optional Google OAuth login for private files or files that need
  authenticated access.
- Tries multiple download methods automatically:
  - OAuth2 access token, when logged in.
  - Google Drive API with API key.
  - `drive.google.com/uc` with cookies.
  - `drive.usercontent.google.com` fallback.
- Saves API key, OAuth client details, and token locally beside the executable.
- Cleans downloaded filenames so they are valid on Windows.
- Shows file sizes and download progress in the console.

## Files

| File | Purpose |
| --- | --- |
| `gdrive_downloader.c` | Main C source code. |
| `gdrive_downloader.exe` | Compiled executable. |
| `gdrive_api_key.txt` | Saved Google API key. Created by the program. |
| `gdrive_client.txt` | Saved OAuth Client ID and Client Secret. Created after `login`. |
| `gdrive_token.txt` | Saved OAuth access token. Created after successful login. |
| `gdrive_<FOLDER_ID>\` | Output folder created when downloading a Drive folder. |

## Requirements

- Windows 10 or Windows 11.
- `curl.exe` available in `PATH`.
  - Windows 10/11 usually includes curl.
- A C compiler, such as MinGW/GCC.
- A Google Cloud project with the Google Drive API enabled.
- A Google API key for folder downloads.
- Optional OAuth Desktop client credentials for authenticated downloads.

## Download And Install Dependencies

### Install GCC With MSYS2

1. Download and install MSYS2:

   ```text
   https://www.msys2.org/
   ```

2. Open `MSYS2 UCRT64` from the Windows Start menu.

3. Update MSYS2:

   ```bash
   pacman -Syu
   ```

   If the terminal asks you to close it, close it, reopen `MSYS2 UCRT64`, then
   run the same command again.

4. Install GCC:

   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc
   ```

5. Check GCC:

   ```bash
   gcc --version
   ```

If you want `gcc` available in normal PowerShell or Command Prompt, add this
folder to your Windows `PATH`:

```text
C:\msys64\ucrt64\bin
```

### Check Curl

Open PowerShell or Command Prompt:

```powershell
curl.exe --version
```

If this fails, install curl from:

```text
https://curl.se/windows/
```

## Build

Open a terminal in the folder containing `gdrive_downloader.c`, then run:

```powershell
gcc .\gdrive_downloader.c -o .\gdrive_downloader.exe -lshell32
```

The `-lshell32` library is required because the program uses `ShellExecuteA` to
open the browser and Windows Explorer.

## Run

```powershell
.\gdrive_downloader.exe
```

On startup, the program checks for `curl.exe`, loads any saved API key, loads any
saved OAuth token, then shows this prompt:

```text
Paste Google Drive link, or type: login / key / quit
>
```

## First-Time Google API Key Setup

Folder downloads require a Google API key because the program must call the
Google Drive API to list files in the folder.

When the program asks for an API key, follow these steps:

1. Go to:

   ```text
   https://console.cloud.google.com
   ```

2. Select or create a Google Cloud project.
3. Open `APIs & Services > Library`.
4. Search for `Google Drive API`.
5. Click `Enable`.
6. Open `APIs & Services > Credentials`.
7. Click `Create Credentials > API key`.
8. Copy the API key.
9. Paste it into the program.

The key is saved locally in:

```text
gdrive_api_key.txt
```

You can update the API key later by typing:

```text
key
```

## Optional OAuth Login

Single public files can often download without login. Use `login` when you need
authenticated access, such as private files shared with your Google account.

At the prompt, type:

```text
login
```

The program will ask for OAuth2 credentials. To create them:

1. Go to:

   ```text
   https://console.cloud.google.com
   ```

2. Select or create a Google Cloud project.
3. Open `APIs & Services > Library`.
4. Search for `Google Drive API` and click `Enable`.
5. Open `APIs & Services > Credentials`.
6. Create an OAuth client ID.
7. Choose `Desktop app` as the application type.
8. Copy the Client ID and Client Secret.
9. Paste both values into the program.

The program saves these values in:

```text
gdrive_client.txt
```

Then it opens a browser for Google login. After approving access, paste the
authorization code back into the terminal. The access token is saved in:

```text
gdrive_token.txt
```

Access tokens can expire. If authenticated downloads stop working, run `login`
again.

## Supported Input

Paste any of these at the prompt:

```text
https://drive.google.com/file/d/FILE_ID/view
https://drive.google.com/drive/folders/FOLDER_ID
https://drive.google.com/open?id=FILE_ID
FILE_ID
```

Raw IDs are treated as file IDs, not folder IDs. For folders, paste a full folder
link containing `/folders/`.

## Downloading A Single File

Example:

```text
https://drive.google.com/file/d/1abcDEFghiJKLmnop/view
```

The program will:

1. Extract the file ID.
2. Try to find the filename using the Drive API or HTTP headers.
3. Save the file beside `gdrive_downloader.exe`.
4. Ask before overwriting an existing file.
5. Ask whether to open the file location after download.

## Downloading A Folder

Example:

```text
https://drive.google.com/drive/folders/1abcDEFghiJKLmnop
```

The program will:

1. Extract the folder ID.
2. Query the Google Drive API.
3. List up to 1000 regular files in that folder.
4. Skip subfolders.
5. Ask whether to download all listed files.
6. Save files into:

   ```text
   gdrive_<FOLDER_ID>\
   ```

7. Show success and failure counts.
8. Ask whether to open the download folder.

## Commands

| Command | Meaning |
| --- | --- |
| `login` | Start OAuth login and save an access token. |
| `key` | Enter or replace the saved Google API key. |
| `apikey` | Same as `key`. |
| `quit` | Exit the program. |
| `exit` | Exit the program. |

## Output Location

The program uses the executable folder as its working location.

| Download type | Save location |
| --- | --- |
| Single file | Same folder as `gdrive_downloader.exe`. |
| Folder | `gdrive_<FOLDER_ID>\` beside `gdrive_downloader.exe`. |

Temporary files such as `gdrive_tmp.dat`, `gdrive_token_resp.json`, and
`gdrive_dl_cookies.txt` may be created during requests and are removed after use.

## Security Notes

- `gdrive_api_key.txt`, `gdrive_client.txt`, and `gdrive_token.txt` are plain
  text files.
- Do not share those files publicly.
- The OAuth scope used by the program is read-only:

  ```text
  https://www.googleapis.com/auth/drive.readonly
  ```

- The program does not upload or modify Google Drive content.
- Downloaded files are written to your local disk.

## Limitations

- Windows-only source code.
- Folder downloads require an API key.
- Folder mode downloads regular files only; subfolders are skipped.
- Folder listing is limited to `MAX_FILES`, currently 1000 files.
- Google Docs, Sheets, and Slides are not exported to Office/PDF formats by this
  program.
- Private files usually require OAuth login.
- Saved OAuth access tokens can expire.
- Very long file paths may fail because the source uses fixed-size buffers.
- The program uses simple JSON parsing, so unexpected API responses may not be
  handled perfectly.

## Troubleshooting

### `curl not found`

Run:

```powershell
curl.exe --version
```

If it fails, install curl or add it to your `PATH`.

### Folder Download Says No API Key

Type:

```text
key
```

Then paste a Google API key from a project where the Google Drive API is enabled.

### API Error Or No Files Found

Check these:

- The Google Drive API is enabled in the same project as the API key.
- The folder link is correct.
- The folder is shared with permission that allows access.
- The folder contains regular files, not only subfolders or Google Docs files.

### Download Failed

Try these:

- Make sure the file is shared publicly, or run `login`.
- Confirm the file ID is correct.
- Delete an expired `gdrive_token.txt` or run `login` again.
- Check that your antivirus or firewall is not blocking `curl.exe`.

### `gcc` Is Not Recognized

Install MSYS2 and GCC, or add this folder to your Windows `PATH`:

```text
C:\msys64\ucrt64\bin
```

## Source Code Overview

| Section | Responsibility |
| --- | --- |
| Constants and globals | Console colors, config names, file limit, saved credentials. |
| Console helpers | Colored banner and prompt output. |
| File helpers | Filename cleanup, size formatting, file existence checks. |
| API key management | Loads, saves, and prompts for `gdrive_api_key.txt`. |
| OAuth login | Saves client credentials and gets an access token. |
| Link parsing | Extracts file or folder IDs from Google Drive links. |
| curl helpers | Runs `curl.exe` and reads responses from temporary files. |
| Download logic | Tries OAuth, API key, cookie, and usercontent download paths. |
| Folder mode | Lists folder files and downloads them into `gdrive_<FOLDER_ID>\`. |
| Single-file mode | Resolves filename and downloads one file beside the executable. |
| Main loop | Handles `login`, `key`, `quit`, and pasted Drive links. |

