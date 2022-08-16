#!/bin/sh

rsync -ruvz -e 'ssh -p 22223' --delete '/home/winrout/labs/os2/' root@localhost:~/
