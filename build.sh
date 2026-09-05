#!/usr/bin/env bash
set -euo pipefail

# Build Fluorine Manager using Docker.
#
# Usage:
#   ./build.sh              # Build portable .tar.gz (default)
#   ./build.sh tarball      # Build portable .tar.gz only
#   ./build.sh installer    # Build self-extracting .bin installer only
#   ./build.sh all          # Build tarball + installer
#   ./build.sh test         # Build and run the standalone test suite
#   ./build.sh usvfs [RUN]  # Test Fluorine and package an exact green USVFS run
#   ./build.sh shell        # Drop into the build container for debugging
#
# Prerequisites: Docker or Podman

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_IMAGE_READY=false
REFERENCE_USVFS_X64_SHA256="2902ec5ac898da59a522b48bc8b6d705758e3b103ef0b7397763688d5a47ceb7"
REFERENCE_USVFS_X86_SHA256="bafb128bbe05084b929b5fa7ea37dac1448477e1a26d86938994f71d554c1ea7"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: Required command not found: $1" >&2
        exit 1
    fi
}

prepare_usvfs_candidate() {
    local checkout="${USVFS_CHECKOUT:-$(dirname "${SCRIPT_DIR}")/usvfs}"
    local repository="${USVFS_REPOSITORY:-SulfurNitride/usvfs}"
    local run_id="${USVFS_RUN_ID:-${2:-}}"
    local source_commit run_list run_data run_status run_conclusion run_head run_url
    local required_job successful_jobs candidate_parent candidate_dir download_dir
    local candidate_relative

    require_command gh
    require_command git
    require_command jq
    require_command file
    require_command sha256sum

    if ! git -C "${checkout}" rev-parse --git-dir >/dev/null 2>&1; then
        echo "ERROR: USVFS checkout not found: ${checkout}" >&2
        exit 1
    fi
    if [ -n "$(git -C "${checkout}" status --porcelain)" ]; then
        echo "ERROR: USVFS checkout has uncommitted changes: ${checkout}" >&2
        exit 1
    fi

    source_commit="$(git -C "${checkout}" rev-parse HEAD)"
    if ! git -C "${checkout}" ls-remote origin |
        cut -f1 | grep -Fxq "${source_commit}"; then
        echo "ERROR: USVFS commit ${source_commit} is not present in an origin ref" >&2
        exit 1
    fi

    if [ -z "${run_id}" ]; then
        run_list="$(gh run list -R "${repository}" --workflow "Build USVFS" \
            --commit "${source_commit}" --limit 20 \
            --json databaseId,createdAt)"
        run_id="$(jq -r 'sort_by(.createdAt) | last | .databaseId // empty' \
            <<<"${run_list}")"
    fi
    if [ -z "${run_id}" ]; then
        echo "ERROR: No Build USVFS workflow run found for ${source_commit}" >&2
        echo "Push the USVFS commit first, then rerun ./build.sh usvfs." >&2
        exit 1
    fi

    run_data="$(gh run view "${run_id}" -R "${repository}" \
        --json status,conclusion,headSha,url,workflowName,jobs)"
    run_head="$(jq -r '.headSha' <<<"${run_data}")"
    if [ "${run_head}" != "${source_commit}" ]; then
        echo "ERROR: Workflow ${run_id} built ${run_head}, expected ${source_commit}" >&2
        exit 1
    fi
    if [ "$(jq -r '.workflowName' <<<"${run_data}")" != "Build USVFS" ]; then
        echo "ERROR: Workflow ${run_id} is not the Build USVFS workflow" >&2
        exit 1
    fi

    run_status="$(jq -r '.status' <<<"${run_data}")"
    if [ "${run_status}" != "completed" ]; then
        echo "=== Waiting for USVFS Windows matrix ${run_id} ==="
        gh run watch "${run_id}" -R "${repository}" --exit-status --interval 30
        run_data="$(gh run view "${run_id}" -R "${repository}" \
            --json status,conclusion,headSha,url,workflowName,jobs)"
    fi

    run_conclusion="$(jq -r '.conclusion' <<<"${run_data}")"
    if [ "${run_conclusion}" != "success" ]; then
        echo "ERROR: USVFS workflow ${run_id} concluded ${run_conclusion}" >&2
        exit 1
    fi

    for required_job in \
        "Build USVFS (x86, Debug)" \
        "Build USVFS (x86, Release)" \
        "Build USVFS (x64, Debug)" \
        "Build USVFS (x64, Release)" \
        "Test USVFS (Debug, x86)" \
        "Test USVFS (Release, x86)" \
        "Test USVFS (Debug, x64)" \
        "Test USVFS (Release, x64)"; do
        successful_jobs="$(jq --arg name "${required_job}" \
            '[.jobs[] | select(.name == $name and .conclusion == "success")] | length' \
            <<<"${run_data}")"
        if [ "${successful_jobs}" -ne 1 ]; then
            echo "ERROR: Required USVFS job was not successful: ${required_job}" >&2
            exit 1
        fi
    done

    run_url="$(jq -r '.url' <<<"${run_data}")"
    candidate_parent="${SCRIPT_DIR}/build/usvfs-candidate/${source_commit}"
    candidate_dir="${candidate_parent}/${run_id}"
    mkdir -p "${candidate_parent}"
    if [ ! -f "${candidate_dir}/fluorine-candidate-build.txt" ]; then
        if [ -e "${candidate_dir}" ]; then
            for required_file in \
                usvfs_x64.dll usvfs_x86.dll \
                usvfs_proxy_x64.exe usvfs_proxy_x86.exe; do
                if [ ! -f "${candidate_dir}/bin/${required_file}" ]; then
                    echo "ERROR: Incomplete candidate directory already exists: ${candidate_dir}" >&2
                    exit 1
                fi
            done
            echo "=== Reusing complete downloaded candidate ${run_id} ==="
        else
            download_dir="$(mktemp -d "${candidate_parent}/.download-${run_id}-XXXXXX")"
            if ! gh run download "${run_id}" -R "${repository}" \
                -n usvfs_Release -D "${download_dir}"; then
                rm -rf -- "${download_dir}"
                exit 1
            fi
            mv -- "${download_dir}" "${candidate_dir}"
        fi
    fi

    for required_file in \
        usvfs_x64.dll usvfs_x86.dll usvfs_proxy_x64.exe usvfs_proxy_x86.exe; do
        if [ ! -f "${candidate_dir}/bin/${required_file}" ]; then
            echo "ERROR: USVFS artifact is missing bin/${required_file}" >&2
            exit 1
        fi
    done
    if [[ "$(file -b "${candidate_dir}/bin/usvfs_x64.dll")" != *"PE32+ executable"* ]] ||
       [[ "$(file -b "${candidate_dir}/bin/usvfs_x64.dll")" != *"x86-64"* ]]; then
        echo "ERROR: Artifact usvfs_x64.dll is not an x64 PE DLL" >&2
        exit 1
    fi
    if [[ "$(file -b "${candidate_dir}/bin/usvfs_x86.dll")" != *"PE32 executable"* ]] ||
       [[ "$(file -b "${candidate_dir}/bin/usvfs_x86.dll")" != *"Intel i386"* ]]; then
        echo "ERROR: Artifact usvfs_x86.dll is not an x86 PE DLL" >&2
        exit 1
    fi
    if [[ "$(file -b "${candidate_dir}/bin/usvfs_proxy_x64.exe")" != *"PE32+ executable"* ]] ||
       [[ "$(file -b "${candidate_dir}/bin/usvfs_proxy_x64.exe")" != *"x86-64"* ]]; then
        echo "ERROR: Artifact usvfs_proxy_x64.exe is not an x64 PE executable" >&2
        exit 1
    fi
    if [[ "$(file -b "${candidate_dir}/bin/usvfs_proxy_x86.exe")" != *"PE32 executable"* ]] ||
       [[ "$(file -b "${candidate_dir}/bin/usvfs_proxy_x86.exe")" != *"Intel i386"* ]]; then
        echo "ERROR: Artifact usvfs_proxy_x86.exe is not an x86 PE executable" >&2
        exit 1
    fi

    {
        printf 'format=1\n'
        printf 'source_commit=%s\n' "${source_commit}"
        printf 'workflow_run=%s\n' "${run_url}"
        printf 'usvfs_x64_sha256=%s\n' \
            "$(sha256sum "${candidate_dir}/bin/usvfs_x64.dll" | cut -d' ' -f1)"
        printf 'usvfs_x86_sha256=%s\n' \
            "$(sha256sum "${candidate_dir}/bin/usvfs_x86.dll" | cut -d' ' -f1)"
    } >"${candidate_dir}/fluorine-candidate-build.txt"

    candidate_relative="${candidate_dir#"${SCRIPT_DIR}/"}"
    FLUORINE_USVFS_RUNTIME_DIR="/src/${candidate_relative}/bin"
    FLUORINE_USVFS_PROVENANCE="/src/${candidate_relative}/fluorine-candidate-build.txt"
    USVFS_CANDIDATE_HOST_DIR="${candidate_dir}"
    export FLUORINE_USVFS_RUNTIME_DIR FLUORINE_USVFS_PROVENANCE

    echo "=== Verified USVFS candidate ==="
    echo "Commit:   ${source_commit}"
    echo "Workflow: ${run_url}"
    sha256sum "${candidate_dir}/bin/usvfs_x64.dll" \
        "${candidate_dir}/bin/usvfs_x86.dll"
}

# Auto-detect container runtime
if command -v podman >/dev/null 2>&1; then
    DOCKER=podman
    VOLUME_SUFFIX=",Z"
elif command -v docker >/dev/null 2>&1; then
    DOCKER=docker
    VOLUME_SUFFIX=""
else
    echo "ERROR: Neither podman nor docker found in PATH"
    exit 1
fi
IMAGE_NAME="fluorine-builder"
CONTAINER_NAME="fluorine-build-$$"

cd "${SCRIPT_DIR}"

# Determine build mode from first argument
REQUESTED_MODE="${1:-tarball}"
BUILD_MODE="${REQUESTED_MODE}"
case "${REQUESTED_MODE}" in
    tarball|installer|all|test|shell) ;;
    usvfs)
        BUILD_MODE=tarball
        echo "=== Testing Fluorine before candidate packaging ==="
        "${SCRIPT_DIR}/build.sh" test
        BUILD_IMAGE_READY=true
        prepare_usvfs_candidate "$@"
        ;;
    *)
        echo "Usage: ./build.sh [tarball|installer|all|test|shell|usvfs [RUN_ID]]"
        echo ""
        echo "  tarball    Build portable .tar.gz"
        echo "  installer  Build self-extracting .bin installer"
        echo "  all        Build tarball + installer"
        echo "  test       Build and run the standalone test suite"
        echo "  shell      Drop into build container"
        echo "  usvfs      Test and package the exact green USVFS commit/run"
        exit 1
        ;;
esac

echo "=== Ensuring build image is up to date ==="
if [ "${BUILD_IMAGE_READY}" = true ]; then
    echo "Reusing image verified by the test build."
else
    ${DOCKER} build -t "${IMAGE_NAME}" docker/
fi

# Persistent ccache directory for faster rebuilds.
CCACHE_DIR="${HOME}/.cache/fluorine-ccache"
mkdir -p "${CCACHE_DIR}"

if [ "${BUILD_MODE}" = "shell" ]; then
    echo "=== Dropping into build container shell ==="
    exec ${DOCKER} run --rm -it \
        -v "${SCRIPT_DIR}:/src:rw${VOLUME_SUFFIX}" \
        -v "${CCACHE_DIR}:/ccache:rw${VOLUME_SUFFIX}" \
        -e CCACHE_DIR=/ccache \
        -w /src \
        --device /dev/fuse \
        --cap-add SYS_ADMIN \
        "${IMAGE_NAME}" \
        bash
fi

echo "=== Starting build (mode: ${BUILD_MODE}) ==="
# BUILD_JOBS controls parallelism (override with `BUILD_JOBS=N ./build.sh`).
# Defaults to all available cores.
BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
${DOCKER} run --rm \
    -v "${SCRIPT_DIR}:/src:rw${VOLUME_SUFFIX}" \
    -v "${CCACHE_DIR}:/ccache:rw${VOLUME_SUFFIX}" \
    -e CCACHE_DIR=/ccache \
    -e BUILD_MODE="${BUILD_MODE}" \
    -e BUILD_JOBS="${BUILD_JOBS}" \
    -e FLUORINE_BUILD_CHANNEL="${FLUORINE_BUILD_CHANNEL:-dev}" \
    -e FLUORINE_BUILD_NUMBER="${FLUORINE_BUILD_NUMBER:-}" \
    -e FLUORINE_BUILD_TIMESTAMP="${FLUORINE_BUILD_TIMESTAMP:-}" \
    -e FLUORINE_BUILD_COMMIT="${FLUORINE_BUILD_COMMIT:-}" \
    -e FLUORINE_USVFS_RUNTIME_DIR="${FLUORINE_USVFS_RUNTIME_DIR:-}" \
    -e FLUORINE_USVFS_PROVENANCE="${FLUORINE_USVFS_PROVENANCE:-}" \
    -w /src \
    --device /dev/fuse \
    --cap-add SYS_ADMIN \
    --name "${CONTAINER_NAME}" \
    "${IMAGE_NAME}" \
    bash /src/docker/build-inner.sh

echo ""
echo "=== Done ==="
echo "Build outputs:"
ls -ldh build/fluorine-manager build/fluorine-manager.bin 2>/dev/null || echo "  (none found)"
echo "Staging: build/staging/"

if [ "${REQUESTED_MODE}" = "usvfs" ]; then
    for candidate_file in usvfs_x64.dll usvfs_x86.dll; do
        cmp -s "${USVFS_CANDIDATE_HOST_DIR}/bin/${candidate_file}" \
            "build/fluorine-manager/usvfs/${candidate_file}"
    done
    cmp -s "${USVFS_CANDIDATE_HOST_DIR}/fluorine-candidate-build.txt" \
        "build/fluorine-manager/usvfs/fluorine-candidate-build.txt"
    echo "Candidate provenance: build/fluorine-manager/usvfs/fluorine-candidate-build.txt"
    sha256sum build/fluorine-manager/usvfs/usvfs_x64.dll \
        build/fluorine-manager/usvfs/usvfs_x86.dll
elif [[ "${REQUESTED_MODE}" =~ ^(tarball|installer|all)$ ]]; then
    reference_runtime_dirs=(build/staging/usvfs)
    if [ "${REQUESTED_MODE}" != "installer" ]; then
        reference_runtime_dirs+=(build/fluorine-manager/usvfs)
    fi
    for reference_runtime_dir in "${reference_runtime_dirs[@]}"; do
        if [ -e "${reference_runtime_dir}/fluorine-candidate-build.txt" ]; then
            echo "ERROR: Candidate provenance survived a reference build: ${reference_runtime_dir}" >&2
            exit 1
        fi
        printf '%s  %s\n' "${REFERENCE_USVFS_X64_SHA256}" \
            "${reference_runtime_dir}/usvfs_x64.dll" | sha256sum -c -
        printf '%s  %s\n' "${REFERENCE_USVFS_X86_SHA256}" \
            "${reference_runtime_dir}/usvfs_x86.dll" | sha256sum -c -
    done
    echo "Reference USVFS payload verified."
fi
