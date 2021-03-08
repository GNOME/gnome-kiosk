#!/bin/sh

if [ ! -e "dbus-interfaces/" ]; then
        echo "Please run from top level source directory" > /dev/stderr
        exit 1
fi

export LANG="C.utf-8"

SERVICE="$1"
INTERFACE="${2:-$1}"
OBJECT_PATH="${3:-/$(echo -n ${INTERFACE} | sed 's![.]!/!g')}"
OUTPUT_FILE="dbus-interfaces/${INTERFACE}.xml"

# The sed goo here:
# 1. unescapes new lines and smashes them into a single non-delimiting character
# 2. unescapes anything else (mainly quote marks)
# 3. strips off the s "..." around the result
# 4. smashes </interface> into a single character, so we can do a non-greedy
#    match for it.
# 5. smashes the interface into a single character, so we can do a non-greedy
#    match for it
# 6. do the aforementioned matches to erase all interfaces from output we
#    weren't asked for
# 7. restore </interface> and the interface to their true selves
# 8. likewise, put the new lines back in the stream
busctl call "${SERVICE}" "${OBJECT_PATH}"                                      \
            org.freedesktop.DBus.Introspectable Introspect                     \
       | sed -e 's!\\n!\r!g'                                                   \
             -e 's!\\\(.\)!\1!g'                                               \
             -e 's!s "\(.*\)"$!\1!'                                            \
             -e 's!'${INTERFACE}'!␚!g'                                         \
             -e 's!</interface>!␙!g'                                           \
             -e 's!<interface name="[^␚]*">[^␙]*␙!!g'                          \
             -e 's!␙!</interface>!g'                                           \
             -e 's!␚!'${INTERFACE}'!g'                                         \
             -e 's!\r[\r ]*\r!\r!g'                                            \
             -e 's!\r!\n!g'                                                    \
       > "${OUTPUT_FILE}"
