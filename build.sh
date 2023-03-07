#!/bin/bash
GIT_COMMIT_HASH=`git log --pretty=format:'%h' -n 1`
g++ -std=c++20 -O3 -Wall -Werror -Wpedantic -DGIT_COMMIT_HASH=\"$GIT_COMMIT_HASH\" feedbackbot.cpp -ldpp -o feedbackbot
