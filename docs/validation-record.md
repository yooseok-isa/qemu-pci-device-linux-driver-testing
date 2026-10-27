# Validation Record

## 2026-10-19 — Host 확인

기준은 `444b11e` 기반 작업 트리이다. 빌드 출력은 임시 디렉터리에 분리했다.

| 환경 | 실행 내용 | 결과 |
| --- | --- | --- |
| GCC 13.3.0 | HAL·CLI 빌드, mock GoogleTest | 빌드 성공, 12건 통과 |
| Buildroot GCC 14.3.0 | 정적 링크 CLI, ioctl smoke, MMIO smoke 교차 빌드 | 빌드 성공 |
| Python 3.12.3, pytest 7.4.4 | pytest 수집 | 로컬 입력 기준 2,022건. 실행 결과가 아님 |
| Host | 셸 문법, QEMU 패치 동기화 검사 | 통과 |

커널 모듈 재빌드, guest 전체 pytest 실행, clean checkout부터의 전체 재현은 수행하지 않았다.

## 2026-09-11 — 과거 Guest 기록

로컬 JUnit의 파일명은 `vnpu-revision-a.xml`이지만 내부 사례명은
`test_random_seed_run_revB`이다. Revision B 무작위 테스트 1,000건에 대해
실패·오류·skip 0건을 기록했다. XML의 host 이름은 `buildroot`이다.

기준 커밋과 실행 명령은 기록되지 않았다. 이 기록은 현재 전체 suite나
Revision A 장치의 통과 근거로 사용할 수 없다. XML은 Git에 포함되지 않는다.

## 재실행과 기록

Guest의 `/mnt/repo`에서 실패 사례의 node ID를 지정한다.
`[번호]`는 실패 보고의 사례 번호로 바꾼다.

```sh
./tools/vnpuctl reset --json
pytest -c python-tests/pytest.ini 'python-tests/test_random_seed.py::test_random_seed_run_revB[번호]' \
  --junitxml=artifacts/pytest/vnpu-rerun.xml
```

테스트 실행 스크립트의 기본 출력 파일명은 실제 revision과 무관하게
`vnpu-revision-a.xml`이다. 실행 시 `--junitxml`로 파일명을 지정한다.

CLI 응답 해석 실패나 예상하지 않은 오류는 종료 코드·stdout·stderr 또는 JSON 응답으로 확인한다.
무작위 테스트는 일부 오류 응답을 `inputs/debug`에 저장하지만, 모든 실패 입력과 로그를 보존하지는 않는다.

## 재현 시 주의점

- 무작위 입력은 seed `5055`를 사용한다. 생성 순서가 바뀌면 사례 번호와 입력의 대응도 달라질 수 있다.
- `random_seed_revA`와 `random_seed_revB`는 길이 8과 16 테스트를 선택하며, 장치 revision을 바꾸지 않는다.
- 고정 입력 수에 따라 전체 수집 건수가 달라진다.
- GitHub Actions는 파일·문법·패치·host 도구 빌드를 검사하며 guest E2E를 실행하지 않는다.

## 근거

- 과거 JUnit: 로컬 `artifacts/pytest/vnpu-revision-a.xml` (Git 제외)
- [pytest 실행 스크립트](../scripts/run-test.sh)
- [CLI 실행 fixture](../python-tests/conftest.py)
- [무작위 테스트](../python-tests/test_random_seed.py)
- [CI 설정](../.github/workflows/ci.yml)
