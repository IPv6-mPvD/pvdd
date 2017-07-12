
if nc -h 2>&1 | grep '\-N' >/dev/null 2>&1
then
	NC="nc -N"
else
	NC="nc"
fi
