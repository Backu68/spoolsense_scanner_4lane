#!/usr/bin/env python3
"""
Mock PrusaLink HTTP server for integration testing SpoolSense scanner.

Simulates /api/v1/status and /api/v1/job endpoints.
Control state via command-line args or HTTP POST to /mock/state.

Usage:
    python3 mock_prusalink.py                    # starts idle
    python3 mock_prusalink.py --port 8080

Then from another terminal:
    curl -X POST http://localhost:8080/mock/state -d '{"state":"printing","job_id":42,"filament_g":9.18}'
    curl -X POST http://localhost:8080/mock/state -d '{"state":"finished"}'
    curl -X POST http://localhost:8080/mock/state -d '{"state":"idle"}'
"""

import json
import argparse
from http.server import HTTPServer, BaseHTTPRequestHandler


class MockState:
    def __init__(self):
        self.reset()

    def reset(self):
        self.printer_state = "IDLE"
        self.job_id = None
        self.job_state = ""
        self.progress = 0.0
        self.filament_g = 0.0
        self.filament_type = "PLA"
        self.nozzle_temp = 0
        self.bed_temp = 0
        self.material_name = ""
        self.download_ref = ""
        # Per-tool (XL)
        self.filament_per_tool = []
        self.filament_type_per_tool = []

    def set_printing(self, job_id: int, filament_g: float = 0,
                     filament_type: str = "PLA", nozzle_temp: int = 215,
                     bed_temp: int = 60, progress: float = 0.0):
        self.printer_state = "PRINTING"
        self.job_id = job_id
        self.job_state = "PRINTING"
        self.progress = progress
        self.filament_g = filament_g
        self.filament_type = filament_type
        self.nozzle_temp = nozzle_temp
        self.bed_temp = bed_temp

    def set_progress(self, progress: float):
        self.progress = progress

    def set_finished(self):
        self.job_state = "FINISHED"
        self.progress = 100.0

    def set_stopped(self):
        self.job_state = "STOPPED"

    def set_idle(self):
        self.reset()

    def status_response(self) -> dict:
        resp = {
            "printer": {
                "state": self.printer_state,
                "temp_nozzle": self.nozzle_temp if self.job_id else 22.0,
                "target_nozzle": self.nozzle_temp if self.job_id else 0,
                "temp_bed": self.bed_temp if self.job_id else 22.0,
                "target_bed": self.bed_temp if self.job_id else 0,
            }
        }
        if self.job_id is not None:
            resp["job"] = {
                "id": self.job_id,
                "progress": self.progress,
            }
        return resp

    def job_response(self) -> dict | None:
        if self.job_id is None:
            return None

        meta = {}
        if self.filament_g > 0:
            meta["filament used [g]"] = self.filament_g
        if self.filament_type:
            meta["filament_type"] = self.filament_type
        if self.nozzle_temp:
            meta["temperature"] = self.nozzle_temp
        if self.bed_temp:
            meta["bed_temperature"] = self.bed_temp
        if self.material_name:
            meta["material_name"] = self.material_name
        if self.filament_per_tool:
            meta["filament used [g] per tool"] = self.filament_per_tool
        if self.filament_type_per_tool:
            meta["filament_type per tool"] = self.filament_type_per_tool

        resp = {
            "state": self.job_state,
            "progress": self.progress,
            "file": {
                "name": "test_print.gcode",
                "meta": meta,
            }
        }
        if self.download_ref:
            resp["file"]["refs"] = {"download": self.download_ref}
        return resp


mock = MockState()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Quieter logging
        pass

    def _send_json(self, code: int, data: dict):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        # Check API key
        api_key = self.headers.get("X-Api-Key", "")
        if not api_key:
            self.send_error(401, "Missing X-Api-Key")
            return

        if self.path == "/api/v1/status":
            self._send_json(200, mock.status_response())

        elif self.path == "/api/v1/job":
            resp = mock.job_response()
            if resp is None:
                self.send_response(204)
                self.end_headers()
            else:
                self._send_json(200, resp)

        elif self.path.startswith("/api/v1/job/"):
            try:
                requested_id = int(self.path.rsplit("/", 1)[1])
            except ValueError:
                self.send_error(400, "Invalid job id")
                return
            resp = mock.job_response()
            if resp is None or requested_id != mock.job_id:
                self.send_error(404)
            else:
                self._send_json(200, resp)

        elif self.path == "/api/v1/info":
            self._send_json(200, {
                "mmu": False,
                "name": "MockPrusa",
                "nozzle_diameter": 0.4,
                "serial": "MOCK123456",
            })

        elif self.path == "/mock/state":
            self._send_json(200, {
                "printer_state": mock.printer_state,
                "job_id": mock.job_id,
                "job_state": mock.job_state,
                "progress": mock.progress,
            })

        else:
            self.send_error(404)

    def do_POST(self):
        if self.path == "/mock/state":
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length)) if length > 0 else {}

            state = body.get("state", "idle")
            if state == "printing":
                mock.set_printing(
                    job_id=body.get("job_id", 1),
                    filament_g=body.get("filament_g", 0),
                    filament_type=body.get("filament_type", "PLA"),
                    nozzle_temp=body.get("nozzle_temp", 215),
                    bed_temp=body.get("bed_temp", 60),
                    progress=body.get("progress", 0.0),
                )
            elif state == "progress":
                mock.set_progress(body.get("progress", 0.0))
            elif state == "finished":
                mock.set_finished()
            elif state == "stopped":
                mock.set_stopped()
            elif state == "idle":
                mock.set_idle()

            # XL per-tool
            if "filament_per_tool" in body:
                mock.filament_per_tool = body["filament_per_tool"]
            if "filament_type_per_tool" in body:
                mock.filament_type_per_tool = body["filament_type_per_tool"]

            print(f"[mock] State → {mock.printer_state} job={mock.job_id} "
                  f"job_state={mock.job_state} progress={mock.progress}% "
                  f"filament={mock.filament_g}g")

            self._send_json(200, {"ok": True})

        elif self.path == "/mock/reset":
            mock.reset()
            print("[mock] Reset to idle")
            self._send_json(200, {"ok": True})

        else:
            self.send_error(404)


def main():
    parser = argparse.ArgumentParser(description="Mock PrusaLink server")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    server = HTTPServer(("0.0.0.0", args.port), Handler)
    print(f"Mock PrusaLink running on http://0.0.0.0:{args.port}")
    print(f"Control: POST http://localhost:{args.port}/mock/state")
    print(f"  {'{'}\"state\":\"printing\",\"job_id\":42,\"filament_g\":9.18{'}'}")
    print(f"  {'{'}\"state\":\"finished\"{'}'}")
    print(f"  {'{'}\"state\":\"idle\"{'}'}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down")
        server.shutdown()


if __name__ == "__main__":
    main()
