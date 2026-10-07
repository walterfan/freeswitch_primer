#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
compose_file="${script_dir}/compose.yaml"
command_name="${1:-up}"

usage() {
    cat <<'EOF'
Usage: bash tutorial-compose.sh [up|down|status|logs]

up      Build and start only the tutorial Compose project
down    Stop and remove only this tutorial project
status  Show tutorial containers
logs    Follow tutorial container logs
clean   Stop the project and remove only generated tutorial certificates
EOF
}

if [[ "${command_name}" == "-h" || "${command_name}" == "--help" ||
      "${command_name}" == "help" ]]; then
    usage
    exit 0
fi

container_cli="${TUTORIAL_CONTAINER_CLI:-}"
if [[ -z "${container_cli}" ]]; then
    if command -v docker >/dev/null 2>&1; then
        container_cli="docker"
    elif command -v podman >/dev/null 2>&1; then
        container_cli="podman"
    fi
fi
[[ -n "${container_cli}" ]] || {
    printf '%s\n' "error: docker or podman is required" >&2
    exit 1
}
"${container_cli}" compose version >/dev/null 2>&1 || {
    printf '%s\n' "error: Docker Compose v2 is required" >&2
    exit 1
}

compose() {
    "${container_cli}" compose -f "${compose_file}" "$@"
}

case "${command_name}" in
    up)
        : "${TUTORIAL_ESL_PASSWORD:?set an isolated lab ESL password}"
        : "${TUTORIAL_MODULE_SO:?set the built mod_tutorial.so path}"
        [[ -f "${TUTORIAL_MODULE_SO}" ]] || {
            printf '%s\n' "error: TUTORIAL_MODULE_SO is not a file" >&2
            exit 2
        }
        compose up --build --detach
        compose ps
        ;;
    down)
        compose down --remove-orphans
        ;;
    clean)
        compose down --remove-orphans
        rm -rf -- "${script_dir}/certs"
        ;;
    status)
        compose ps
        ;;
    logs)
        compose logs --follow --no-color
        ;;
    *)
        printf '%s\n' "error: unsupported command: ${command_name}" >&2
        usage >&2
        exit 2
        ;;
esac
