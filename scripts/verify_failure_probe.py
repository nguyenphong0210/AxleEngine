"""Validate only an exact native intentional-failure event, exit 1 and bounded process."""
import argparse
from pathlib import Path
import tempfile
from run_jobs_ecs_harness import run_scenario

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary",type=Path)
parser.add_argument("--case",required=True)
args=parser.parse_args()
workspace=Path(tempfile.mkdtemp(prefix="axle-failure-probe-"))
result=run_scenario([str(args.binary.resolve()),"--self-test-failure"],args.case,5,"PROBE_FAILURE",workspace)
print(result["status"],result["log"])
raise SystemExit(0 if result["status"]=="PROBE_VERIFIED" and not result["unexpected"] else 1)
