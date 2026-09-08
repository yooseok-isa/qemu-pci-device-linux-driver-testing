import pytest


@pytest.mark.basic
def test_info(run_vnpuctl):
    proc, data = run_vnpuctl("info")

    assert proc.returncode == 0

    assert data["status"] == "success"
    assert data["command"] == "info"
    assert data["abi_version"] == 1
    assert data["device_id"] == 0x564E5055

@pytest.mark.revB
def test_info_revB(run_vnpuctl):
    proc, data = run_vnpuctl("info")

    assert proc.returncode == 0

    assert data["status"] == "success"
    assert data["command"] == "info"
    assert data["abi_version"] == 1
    assert data["device_id"] == 0x564E5055
    assert data["capabilities"] == 0x7D
    assert data["revision"] == 2
