#!/bin/bash

for LEVEL in 0 1 2 3 DEBUG_ERROR DEBUG_LOW DEBUG_MID DEBUG_HIGH DEBUG_TRACE; do
    for COMMAND in DEBUG_PRINT DEBUG_PRINTLN DEBUG_VALUE DEBUG_VALUELN DEBUG_HEX DEBUG_HEXVAL DEBUG_HEXVALLN; do
        if [ $LEVEL = "DEBUG_ERROR" ]; then
            NEWLEVEL=1
        elif [ $LEVEL = "DEBUG_LOW" ]; then
            NEWLEVEL=2
        elif [ $LEVEL = "DEBUG_MID" ]; then
            NEWLEVEL=3
        elif [ $LEVEL = "DEBUG_HIGH" ]; then
            NEWLEVEL=4
        elif [ $LEVEL = "DEBUG_TRACE" ]; then
            NEWLEVEL=5
        else
            NEWLEVEL=`expr $LEVEL + 1`
        fi

        PATTERN="s/${COMMAND}(${LEVEL} ,/DEBUG${NEWLEVEL}_VALUE(/g"

        echo "Level:" $LEVEL " Command:" $COMMAND " Pattern:" $PATTERN
        
        find . -name "*.cpp" -o -name "*.ino" | xargs sed -i bak1 "$PATTERN"
    done
done
