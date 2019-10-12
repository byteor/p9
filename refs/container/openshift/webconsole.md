# OpenShift WebConsole

```text
+-------------------------+                                              +-----------------------+
| BROWSER                 |                                              | MASTER                |
|                         |                                              |                       |
| +--------------------+  |     load static assets (JS, CSS, HTML)       |  +------------------+ |
| |     Page Load      |--|----------------------------------------------|->|  Web Console Pod | |
| +--------------------+  |                                              |  +------------------+ |
|                         |                                              |                       |
| +--------------------+  |  Request initial lists of resources from API |  +------------------+ |
| |     JS Runtime     |--|----------------------------------------------|->|  API Server      | |
| |                    |<-|----------------------------------------------|--|                  | |
| +--------------------+  |  Establish websocket connections to watch    |  +------------------+ |
|                         |         for changes to resources             |                       |
+-------------------------+                                              +-----------------------+
```