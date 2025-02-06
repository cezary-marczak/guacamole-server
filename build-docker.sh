#!/usr/bin/env bash

BASEDIR=$(dirname "$0")

if [ -z "$1" ]; then
  echo "Usage: $0 <FreeRDP version>" >&2
  echo "Please provide a FreeRDP version" >&2
  exit 1
fi
FREERDP_VERSION="$1"

GLOBAL_GITCONFIG="${HOME}/.gitconfig"
DOCKER_GITCONFIG="${BASEDIR}/.dgitconfig"

if [ -f "${GLOBAL_GITCONFIG}" ]; then
  echo "Git config file exists"
  cp -f "${GLOBAL_GITCONFIG}" "${DOCKER_GITCONFIG}"
else
  echo "Git config file does not exist"
  touch "${DOCKER_GITCONFIG}"
fi

cat "${BASEDIR}/.git/config" >> "${DOCKER_GITCONFIG}"

while IFS= read -r line; do
#    numdots=$(grep -o '\.' <<< "$line" | wc -l)

#    if [[ $numdots -eq 1 ]]; then
        line="${line%.*}"
#    fi
    git config --file="${DOCKER_GITCONFIG}" --remove-section "$line"

done < <(git config --file="${DOCKER_GITCONFIG}" --list --name-only | \
         grep -E '^(branch|remote|submodule)\.')

DOCKER_GITCONFIG=$(basename "${DOCKER_GITCONFIG}")
DOCKER_GITCONFIG="./${DOCKER_GITCONFIG}"
docker build --build-arg DOCKER_GITCONFIG="${DOCKER_GITCONFIG}" \
             --build-arg WITH_FREERDP="${FREERDP_VERSION}" \
             --platform linux/amd64 -t guacamole-server-native "${BASEDIR}"