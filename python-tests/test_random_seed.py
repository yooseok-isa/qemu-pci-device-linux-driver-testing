from pathlib import Path
import json
import random
import pytest
import errno

from reference_model import dot_i8

NUM_TEST=1000
rng = random.Random(5055)

# INPUT_DIR = Path(__file__).resolve().parent / "inputs" / "temp"
DEBUG_DIR = Path(__file__).resolve().parent / "inputs" / "debug"

def make_json_input_file():
    return {
        "input_a" : [rng.randint(-128, 127) for _ in range(8)],
        "input_b" : [rng.randint(-128, 127) for _ in range(8)],
        "timeout_ms" : rng.randint(125, 1000),
    } 

def make_json_input_file_revB():
    return {
        "input_a" : [rng.randint(-128, 127) for _ in range(16)],
        "input_b" : [rng.randint(-128, 127) for _ in range(16)],
        "timeout_ms" : rng.randint(125, 1000),
    } 

def reset_device(run_vnpuctl):
    run_vnpuctl("reset", check=False)

test_cases = [make_json_input_file() for _ in range(NUM_TEST)]
test_cases_revB = [make_json_input_file_revB() for _ in range(NUM_TEST)]

@pytest.fixture(scope="module")
def gen_input_dir(tmp_path_factory):
    input_dir = tmp_path_factory.mktemp("inputs")
    return input_dir

@pytest.mark.random_seed_revA
@pytest.mark.parametrize(
        "json_data, case_num",
        [(json_data, i) for i, json_data in enumerate(test_cases, start=0)],
        ids=[f"{i}" for i in range(NUM_TEST)],
        )
def test_random_seed_run_revA(run_vnpuctl, json_data, case_num, gen_input_dir):
    
    # INPUT_JSON_FILE = INPUT_DIR / "random_input.json"
    INPUT_DIR = gen_input_dir / "inputs"
    INPUT_DIR.mkdir(parents=True, exist_ok=True)

    INPUT_JSON_FILE = INPUT_DIR / f"random_input_{case_num}.json"
    expected = dot_i8(test_cases[case_num]["input_a"], test_cases[case_num]["input_b"])

    with INPUT_JSON_FILE.open("w", encoding="utf-8") as f:
        json.dump(json_data, f, indent=2)
    
    proc, data = run_vnpuctl("run-dot", "--input", str(INPUT_JSON_FILE), check=False)
    
    if data["command"] != "run-dot":
        debug_file = DEBUG_DIR / f"debug_unexpected_case_{case_num}.json"
        debug_file.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
        pytest.fail(f"unexpected command : {data.get('command')}")
    else :
        assert data["command"] == "run-dot"
        if(data["status"] == "success"):
            assert data["result"] == expected
            assert data["driver_status"] == "ok"
            assert data["device_error"] == 0

            # errno print schema
            # command
            # status
            # error_type
            # message
            # errno
            # device_error

        if(data["status"] == "error"):
            if data["errno"] == errno.ETIMEDOUT:
                pass
            elif data["errno"] == errno.EFAULT:
                pass
            elif data["errno"] == errno.EINVAL:
                pass
            elif data["errno"] == errno.EIO:
                pass
            else :
                debug_file = DEBUG_DIR / f"debug_error_case_{case_num}.json"
                debug_file.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
                pytest.fail(f"unexpected error(message, errno): {data.get('message'), data.get('errno')}")

    reset_device(run_vnpuctl);

@pytest.mark.random_seed_revB
@pytest.mark.parametrize(
        "json_data, case_num",
        [(json_data, i) for i, json_data in enumerate(test_cases_revB, start=0)],
        ids=[f"{i}" for i in range(NUM_TEST)],
        )
def test_random_seed_run_revB(run_vnpuctl, json_data, case_num, gen_input_dir):
    
    # INPUT_JSON_FILE = INPUT_DIR / "random_input.json"
    INPUT_DIR = gen_input_dir / "inputs"
    INPUT_DIR.mkdir(parents=True, exist_ok=True)

    INPUT_JSON_FILE = INPUT_DIR / f"random_input_{case_num}.json"
    expected = dot_i8(test_cases_revB[case_num]["input_a"], test_cases_revB[case_num]["input_b"])

    with INPUT_JSON_FILE.open("w", encoding="utf-8") as f:
        json.dump(json_data, f, indent=2)
    
    proc, data = run_vnpuctl("run-dot", "--input", str(INPUT_JSON_FILE), check=False)
    
    if data["command"] != "run-dot":
        debug_file = DEBUG_DIR / f"debug_unexpected_case_{case_num}.json"
        debug_file.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
        pytest.fail(f"unexpected command : {data.get('message')}")
    else :
        assert data["command"] == "run-dot"
        if(data["status"] == "success"):
            assert data["result"] == expected
            assert data["driver_status"] == "ok"
            assert data["device_error"] == 0

            # errno print schema
            # command
            # status
            # error_type
            # message
            # errno
            # device_error

        if(data["status"] == "error"):
            if data["errno"] == errno.ETIMEDOUT:
                pass
            elif data["errno"] == errno.EFAULT:
                pass
            elif data["errno"] == errno.EINVAL:
                pass
            elif data["errno"] == errno.EIO:
                pass
            else :
                debug_file = DEBUG_DIR / f"debug_error_case_{case_num}.json"
                debug_file.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
                pytest.fail(f"unexpected error(message, errno): {data.get('message'), data.get('errno')}")

    reset_device(run_vnpuctl);
