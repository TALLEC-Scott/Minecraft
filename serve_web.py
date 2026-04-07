#!/usr/bin/env python3
"""HTTP server with Cross-Origin Isolation headers for SharedArrayBuffer (pthreads)."""
import http.server
import os
import sys
import functools

class COIHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()

port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
directory = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build_web")
Handler = functools.partial(COIHandler, directory=directory)
server = http.server.HTTPServer(("", port), Handler)
print(f"Serving {directory} on http://localhost:{port} with Cross-Origin Isolation")
server.serve_forever()
