set day=%Date:~0,2%
set month=%Date:~3,2%
set year=%Date:~8,2%
mkdir aaa_qconsole\%year%\%month%\%day%
set hour=%time:~0,2%
set minute=%time:~3,2%
rename qconsole.log %hour%_%minute%.log
move %hour%_%minute%.log aaa_qconsole\%year%\%month%\%day%
exit