"""Report timings; assert workload/ownership, never a hardware-dependent FPS threshold."""
import json
import os
from pathlib import Path
import re
import subprocess
import time

out = Path(os.environ["NEST_OUT"])

def call(method, *args, probe=False):
    path, interface = ("/KrellisPaintProbe", "org.kde.kwin.KrellisPaintProbe") if probe else ("/Krellis", "org.kde.kwin.KWin.KrellisEffect")
    return subprocess.check_output(["qdbus6", "org.kde.KWin", path, interface + "." + method, *map(str, args)], text=True).strip()

def percentile(values, fraction):
    values = sorted(values)
    return round(values[min(len(values) - 1, int((len(values) - 1) * fraction))], 3) if values else None

results = []
for count in map(int, os.environ.get("ZOOM_LOAD_COUNTS", "25 50 100").split()):
    client = subprocess.Popen([os.environ["NEST_WORK"] + "/client", str(count)])
    try:
        for _ in range(240):
            dump = call("dump")
            if len(re.findall(r"^p\d+ ", dump, re.M)) == count:
                break
            time.sleep(.25)
        else:
            raise AssertionError(f"expected {count} managed windows")
        time.sleep(2)
        (out / f"layout-{count}.txt").write_text(call("dump"))
        # Alternate overview and the deepest leaf, exercising both hidden tiles and labels.
        samples = {"cpuMs": [], "intervalMs": []}
        cameras = {}
        native_requests = 0
        for _ in range(4):
            for method, args in (("focusIndex", (count - 1,)), ("overview", ())):
                before_requests = int(re.search(r'NATIVE-REQUESTS (\d+)', call('dump'))[1])
                assert call("timing", "true", probe=True) == "true"
                call(method, *args)
                time.sleep(.65)
                call("timing", "false", probe=True)
                data = json.loads(call("timingJson", probe=True))
                for key in samples:
                    samples[key].extend(data[key])
                snapshot = call("dump")
                native_requests += int(re.search(r'NATIVE-REQUESTS (\d+)', snapshot)[1]) - before_requests
                cameras[method] = re.search(r"^camera (.+)$", snapshot, re.M).group(1)
                if method == "focusIndex":
                    (out / f"focused-{count}.txt").write_text(snapshot)
                    target_row = re.findall(r'^p\d+ .*$', snapshot, re.M)[count - 1]
                    target = re.search(r' screen [-\d.]+,[-\d.]+ ([-\d.]+)x([-\d.]+)', target_row)
                    assert target and all(float(size) > 0 for size in target.groups()), "focused tile has no area"
        assert len(samples["cpuMs"]) >= 8, "no rendered frame samples"
        assert cameras["focusIndex"] != cameras["overview"], "camera did not zoom"
        assert len(re.findall(r"^p\d+ ", call("dump"), re.M)) == count
        result = dict(windows=count, frames=len(samples["cpuMs"]),
                      nativeRequests=native_requests,
                      cpuMedianMs=percentile(samples["cpuMs"], .5), cpuP95Ms=percentile(samples["cpuMs"], .95),
                      intervalMedianMs=percentile(samples["intervalMs"], .5), intervalP95Ms=percentile(samples["intervalMs"], .95))
        results.append(result)
        (out / f"timings-{count}.json").write_text(json.dumps(samples))
        print("PASS zoom-load", json.dumps(result), flush=True)
    finally:
        client.terminate()
        client.wait(timeout=10)
        time.sleep(2)
(out / "zoom-load.json").write_text(json.dumps(results, indent=2))
