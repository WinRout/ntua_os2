#!/bin/sh

rsync -ruvz -e 'ssh -p 22223' --delete '/home/winrout/labs/os2/' root@83.212.76.14:~/nikitas
