"""One-time Spotify OAuth helper; uses only Python's standard library."""
from http.server import BaseHTTPRequestHandler, HTTPServer
import argparse
import base64
import json
import secrets
import urllib.parse
import urllib.request
import webbrowser


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("client_id")
    parser.add_argument("client_secret")
    args = parser.parse_args()
    redirect = "http://127.0.0.1:8888/callback"
    state = secrets.token_urlsafe(24)
    result = {}

    class Callback(BaseHTTPRequestHandler):
        def do_GET(self):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            result.update({key: values[0] for key, values in query.items()})
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"Spotify autorizado. Ya puedes cerrar esta ventana.")

        def log_message(self, *_):
            pass

    server = HTTPServer(("127.0.0.1", 8888), Callback)
    url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "client_id": args.client_id,
        "response_type": "code",
        "redirect_uri": redirect,
        "scope": "user-read-currently-playing user-read-playback-state user-modify-playback-state",
        "state": state,
    })
    print("Abriendo Spotify; configura esta redirect URI en tu app:", redirect)
    webbrowser.open(url)
    while "code" not in result and "error" not in result:
        server.handle_request()
    server.server_close()
    if result.get("state") != state or "code" not in result:
        raise SystemExit("Autorización cancelada o state inválido")

    credentials = base64.b64encode(f"{args.client_id}:{args.client_secret}".encode()).decode()
    data = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": result["code"],
        "redirect_uri": redirect,
    }).encode()
    request = urllib.request.Request("https://accounts.spotify.com/api/token", data=data)
    request.add_header("Authorization", "Basic " + credentials)
    with urllib.request.urlopen(request) as response:
        token = json.load(response)["refresh_token"]
    print('\n#define SPOTIFY_REFRESH_TOKEN "' + token + '"')


if __name__ == "__main__":
    main()

