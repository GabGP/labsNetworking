# opus-explains.md

Explanation of **Laboratorio #02 – HTTP RFC 2616** (Ciencias de la Computación VIII):
what the assignment wants, what the delivered code already does, and what you still
have to build.

---

## 1. What the lab is asking

Build a **multi-threaded HTTP/1.1 web server** that correctly serves the test site in
`./www/`. The networking plumbing (TCP sockets, thread pool, logging) is **given to you
already working**. Your job is to implement the two halves of the HTTP conversation:

### `Request` — parse what the client sends
- Read the request line, headers, and body from the socket.
- Classify the method: **GET, POST, HEAD**.
- Store headers in a suitable structure (`HashMap` / `ArrayList`).
- Extract the body and handle several content types:
  `application/x-www-form-urlencoded`, `application/json`, `multipart/form-data`,
  `text/plain`.
- Tokenize form data:
  - **GET** → parameters live in the URL **query string** (`/page.cc8?name=...&email=...`).
  - **POST** → parameters live in the **body**, in the format named by `Content-Type`.
- Hand the parsed information to `Response`, and produce proper error info when the
  request is malformed.

### `Response` — build what the server sends back
- Generate an HTTP response **based on the actual request** (route by path/method).
- Serve different content types correctly: **HTML, CSS, JS, JSON, JPEG/PNG/GIF, fonts**, etc.
- Set a **correct `Content-Length`** (in bytes) and send the body properly (text *and*
  binary).
- Add the headers a browser needs so it does **not** treat the reply as an error
  (`Content-Type`, `Content-Length`, status line, etc.).
- **Preprocess `.cc8` files**: these are HTML files containing tags of the form
  `{keyValue}`. Each tag must be replaced by the matching form field
  (`<input name="keyValue">`) coming from the GET query or POST body. Output is served as
  `text/html` (like Apache turning `.php` into HTML).

### Rules / grading
- Must compile with the provided `Makefile` on **Java 21 or 22**.
- **Do NOT modify** `Server.java`, `ThreadServer.java`, `FormatterWebServer.java`.
- You may modify `Request.java` and `Response.java` **without changing the method
  signatures**, and you may add new helper classes (remember to add them to the `Makefile`).
- Automatic **zero** if: it doesn't compile with the Makefile, the LOGs lose the required
  format, the client console throws an error, you ignore the functional instructions, or
  you change the given structure.
- Test scenarios in `./www/`: `index.html` (index), `test01/` (CSS + images + font),
  `test02/` (GET/POST forms with several enctypes), `test03/` (full working website).

---

## 2. What the current code already does (and how)

### Given, do-not-touch infrastructure
| File | Role | How it works |
|------|------|--------------|
| `Lab02.java` | Entry point | Parses CLI args (`-threads`, `-port`, `-delay`, `-help`) into a `HashMap`, prints help or constructs and starts a `Server`. |
| `Server.java` | Bootstrap | Applies defaults (threads=2, port=1000, delay=5), opens a `ServerSocket`, creates a fixed `ThreadPoolExecutor`, and launches N `ThreadServer` runnables. |
| `ThreadServer.java` | Per-thread loop | Sets up a per-thread `Logger` using `FormatterWebServer`. In an infinite loop it `accept()`s a socket, wraps it in a `BufferedReader` (in) / `PrintStream` (out), then calls `threadResponse.sendData(LOGGER, dataOut, id, threadRequest.getData(LOGGER, dataIn, id))`, closes the socket, logs elapsed time, and sleeps `delay` seconds. |
| `FormatterWebServer.java` | Log format | Custom `java.util.logging.Formatter`: `yyMMddHHmmssSSS Class.method()\tLEVEL: message`. This is the "formato establecido" you must not break. |

The key handoff: **`Request.getData(...)` returns an `Object`, which `ThreadServer`
passes straight into `Response.sendData(...)`.** That `Object` is the bridge between the
two classes.

### `Request.java` — **essentially complete** ✅
It is already fully implemented and does the following (in `getData`):
1. Reads the first line; guards against empty/malformed request lines → error **400**.
2. Splits `METHOD URI VERSION`, stores them in an `HttpRequestData` object.
3. Classifies the method; anything other than GET/POST/HEAD → error **405**.
4. Calls `RequestHelper.parseUriAndQueryParams(...)` to split the path from the `?query`
   and parse GET params.
5. Loops reading header lines until the blank line, storing each into a
   `Map<String,String>` (keys lower-cased); tracks `Content-Length` and `Content-Type`.
6. If `Content-Length > 0`, reads exactly that many characters as the body, then calls
   `RequestHelper.parseRequestBody(...)`.
7. Logs the raw request and fills `rawRequestString`. Catches exceptions → error **500**.

### `HttpRequestData.java` — the data carrier ✅
A plain POJO holding everything `Response` needs: `method`, `path`, `rawUri`,
`httpVersion`, `headers` map, **`params` map** (unified GET+POST fields), `body`,
`errorCode` / `errorMessage`, and a convenience `hasError()` (`errorCode >= 400`). This is
the concrete type behind the `Object` returned to `Response`.

### `RequestHelper.java` — parsing helpers ✅
- `parseUriAndQueryParams` — separates `path` from query and parses the query.
- `parseRequestBody` — dispatches on `Content-Type`:
  - `application/x-www-form-urlencoded` → `parseFormUrlEncoded` (splits on `&` and `=`,
    URL-decodes).
  - `application/json` → `parseJsonBody` (naive flat `{"k":"v"}` parser).
  - `multipart/form-data` → `parseMultipartFormData` (splits on the boundary, reads
    `name="..."` and the part value).
- All parsed fields land in the same `params` map.

### `Response.java` — **NOT done, static stub** ❌
This is the placeholder you must replace. Right now it **ignores the request entirely**
and always:
- reads `./www/index.html`,
- replaces `{fieldTest_DEMO}` with a random number,
- writes a hard-coded `200 OK / text/html` response,
- uses `fileData.length()` (character count, not bytes) for `Content-Length`.

So today the server answers *the same HTML to every request*, cannot serve CSS, images,
fonts, `.cc8` files, errors, or HEAD requests.

**Bottom line:** the *Request* side of the lab is basically finished; the real work left
is the *Response* side (plus a couple of edge cases in Request).

---

## 3. What's missing and how to implement it

The center of gravity is **`Response.java`**. Rewrite `sendData` so it uses the parsed
request instead of ignoring it.

### 3.1 Cast the request and handle errors first
```java
HttpRequestData req = (HttpRequestData) request;

if (req.hasError()) {
    sendError(LOGGER, dataOut, nThreadServer, req.getErrorCode(), req.getErrorMessage());
    return;
}
```
Build a small `sendError(...)` that emits a minimal HTML error page (404/400/405/500) with
a correct status line, `Content-Type: text/html`, and a correct `Content-Length`.

### 3.2 Resolve the path to a real file (routing)
- Start from `req.getPath()`.
- Map `/` (and any path ending in `/`) to that directory's `index.html`
  (e.g. `/test02/` → `www/test02/index.html`).
- Prefix everything with the `www` root: `Path file = Paths.get("www", req.getPath());`
- **Guard against path traversal** (reject `..`). Return **404** if the file does not
  exist / is a directory with no index.

### 3.3 Determine the Content-Type from the extension
Write a helper `contentTypeFor(String path)`:

| Extension | Content-Type |
|-----------|--------------|
| `.html`, `.htm`, **`.cc8`** | `text/html; charset=utf-8` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.json` | `application/json` |
| `.png` | `image/png` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.gif` | `image/gif` |
| `.svg` | `image/svg+xml` |
| `.ico` | `image/x-icon` |
| `.ttf` / `.otf` / `.woff` / `.woff2` | `font/ttf` … / `font/woff2` |
| default | `application/octet-stream` |

> Add `; charset=utf-8` to the text types (`text/html`, `text/css`). The test pages are
> in Spanish with accented characters (`ó`, `í`, `–`); without the charset the browser can
> render them as mojibake. Include the `.ico` row because browsers automatically request
> `/favicon.ico` — map it so that hit is served (or cleanly 404'd) instead of falling
> through unhandled.

### 3.4 Read the body as **bytes**, not String  ⚠️ (most important detail)
The current stub sends everything as text. Images and fonts are binary and will be
corrupted if pushed through `String`. Do this instead:
```java
byte[] body = Files.readAllBytes(file);   // works for text AND binary
```
- `Content-Length` must be **`body.length`** (byte count), not `String.length()`.
- Write the head as text, then the body as raw bytes on the same `PrintStream`:
```java
String head = String.join("\r\n",
    "HTTP/1.1 200 OK",
    "Content-Type: " + type,
    "Content-Length: " + body.length,
    "Connection: close",
    "", "");           // blank line separates head from body
dataOut.write(head.getBytes(StandardCharsets.UTF_8));
dataOut.write(body);   // raw bytes — do NOT use print() for binary
dataOut.flush();
```

### 3.5 Preprocess `.cc8` files (the graded feature)
When the resolved file ends in `.cc8`:
1. Read it as text (`new String(bytes, UTF_8)`).
2. For every entry in `req.getParams()`, replace `{key}` with its value:
   ```java
   for (Map.Entry<String,String> e : req.getParams().entrySet())
       html = html.replace("{" + e.getKey() + "}", e.getValue());
   ```
3. Optionally clear any leftover `{...}` tags (fields the form didn't send) so they don't
   show raw to the user.
4. Serve the result as `text/html` with the byte-length of the substituted string.

This is what makes `test02/displayFields.cc8` show the submitted `{name}` / `{email}`, and
`test03/PrimerTest.cc8` show `{name}/{email}/{subject}/{massage}`. Because `Request` already
merges GET query params and POST body params into the **same** `params` map, the same code
path works for both methods.

### 3.6 Handle the HEAD method
For `HEAD`, compute and send the **headers exactly as for GET** (including the correct
`Content-Length`) but **do not write the body**.

### 3.7 Keep the logs in the required format
Preserve the existing `LOGGER.info("(" + nThreadServer + ") response: ...")` and
`request:` log lines. Don't change `FormatterWebServer`. Losing the log format is an
automatic zero.

### 3.8 Minor gaps / edge cases in `Request` (optional hardening)
`Request.java` works, but note two subtleties you may want to tighten:
- The body is read into a `char[Content-Length]` via a `BufferedReader`. `Content-Length`
  counts **bytes**; for multibyte UTF-8 bodies the char count can differ. For the ASCII
  form data in these tests it's fine, but be aware if you feed it non-ASCII.
- The JSON and multipart parsers are intentionally simple (flat, no nested objects, no file
  uploads). Enough for `test02`/`test03`, but not general-purpose.

### 3.9 Suggested implementation order
1. Cast + error/404 short-circuit.
2. Path routing (`/` → `index.html`, `www` root, traversal guard).
3. Extension → Content-Type map.
4. Byte-based read + correct `Content-Length` + binary write. → **test01 now works.**
5. `.cc8` tag substitution using `params`. → **test02 & test03 forms now work.**
6. HEAD support and error pages.
7. Rebuild with `make`, run `make run`, and click through `index.html`, `test01`,
   `test02` (each enctype), and `test03`, watching the `logs/` output.

### 3.10 Build reminder
If you add helper classes, list them in the `Makefile` `CLASSES` block so `make` compiles
them (it currently lists `HttpRequestData`, `Request`, `RequestHelper`, `Response`,
`FormatterWebServer`, `ThreadServer`, `Server`, `Lab02`).
