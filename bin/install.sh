#!/usr/bin/env bash

KERNEL_NAME="$(uname -s)"
PACKAGE_DIR="$(dirname $(readlink -f ${0}))"
PACKAGE_NAME="$(cat ${PACKAGE_DIR}/pkgname.cfg)"

INSTALL_Y=""
INSTALL_DIR="/usr/local/bin"

echo -e -n "install \033[1m‘${PACKAGE_NAME}’\033[0m to \033[1m‘${INSTALL_DIR}/’\033[0m? [y/n]: "
read -p "" INSTALL_Y
if [ ! "${INSTALL_Y}" = "y" ]; then
    echo -e "\033[1;34mwarning:\033[0m command \033[1m‘${PACKAGE_NAME}’\033[0m was not installed, use with \033[1m‘${PACKAGE_DIR}/driver.sh’\033[0m"
    exit 0
fi

if [ ! -d "${INSTALL_DIR}" ]; then
    sudo mkdir -p ${INSTALL_DIR}/
fi
sudo find ${INSTALL_DIR}/ -maxdepth 1 -name "${PACKAGE_NAME}" -type l -delete
if [ ${?} -ne 0 ]; then
    echo -e "\033[0;31merror:\033[0m installation failed" 1>&2
    exit 1
fi

sudo ln -s ${PACKAGE_DIR}/driver.sh ${INSTALL_DIR}/${PACKAGE_NAME}
if [ ${?} -ne 0 ]; then
    echo -e "\033[0;31merror:\033[0m installation failed" 1>&2
    exit 1
fi
echo -e "created symlink \033[1;36m${INSTALL_DIR}/${PACKAGE_NAME}\033[0m -> \033[1;32m${PACKAGE_DIR}/driver.sh\033[0m"

if [ -d "${PACKAGE_DIR}/libc/" ]; then
    if [ ! -z "$(ls ${PACKAGE_DIR}/libc/)" ]; then
        RC_FILE="bashrc"
        if [[ "${KERNEL_NAME}" == "Darwin"* ]]; then
            RC_FILE="zshrc"
        elif [[ "${KERNEL_NAME}" == "FreeBSD"* ]]; then
            RC_FILE="shrc"
        fi
        if [ ! -f "${HOME}/.${RC_FILE}" ]; then
            if [ -f "${HOME}/.bashrc" ]; then
                RC_FILE="bashrc"
            elif [ -f "${HOME}/.zshrc" ]; then
                RC_FILE="zshrc"
            elif [ -f "${HOME}/.shrc" ]; then
                RC_FILE="shrc"
            elif [ -f "${HOME}/.tcshrc" ]; then
                RC_FILE="tcshrc"
            elif [ -f "${HOME}/.cshrc" ]; then
                RC_FILE="cshrc"
            elif [ -f "${HOME}/.kshrc" ]; then
                RC_FILE="kshrc"
            fi
        fi
        if [[ "${RC_FILE}" == *"cshrc" ]]; then
            sudo echo 'if ( ! ${?LD_LIBRARY_PATH} ) then; setenv LD_LIBRARY_PATH; endif' >> ~/.${RC_FILE}
            sudo echo 'setenv LD_LIBRARY_PATH "${LD_LIBRARY_PATH}:'"${PACKAGE_DIR}/libc/\"" >> ~/.${RC_FILE}
        else
            sudo echo 'export LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:'"${PACKAGE_DIR}/libc/\"" >> ~/.${RC_FILE}
        fi
        if [ ${?} -ne 0 ]; then
            echo -e "\033[0;31merror:\033[0m libc installation failed" 1>&2
            exit 1
        fi
        echo -e "exported \033[1m‘${PACKAGE_DIR}/libc/’\033[0m library path to ~/.${RC_FILE}"
    else
        rm -r ${PACKAGE_DIR}/libc/
    fi
fi

echo -e "installation was successful, use with command \033[1m‘${PACKAGE_NAME}’\033[0m"
exit 0
