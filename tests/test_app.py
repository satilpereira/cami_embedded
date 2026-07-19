from pytest_embedded import Dut


def test_app_boots(dut: Dut) -> None:
    dut.expect_exact('cami_embedded application started')
