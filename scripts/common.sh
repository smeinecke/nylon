# shellcheck shell=bash
BOLDGREEN="\e[1;32m"
BOLDRED="\e[1;31m"
ENDCOLOR="\e[0m"

function statusline {
    echo -e "${BOLDGREEN}[*]${ENDCOLOR} \e[1m$*${ENDCOLOR}"
}

function errorline {
    echo -e "${BOLDRED}[*]${ENDCOLOR} \e[1m$*${ENDCOLOR}"
}
