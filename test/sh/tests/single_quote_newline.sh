nl='
'
# Compare by checking the actual newline character is present
if [ $nl = "
" ]; then
    printf "test 1:    newline preserved\n"
else
    printf "test 1:    newline not preserved\n"
fi
if [ "$nl" = "
" ]; then
    printf "test 2:    newline preserved\n"
else
    printf "test 2:    newline not preserved\n"
fi
if [ ${nl} = "
" ]; then
    printf "test 3:    newline preserved\n"
else
    printf "test 3:    newline not preserved\n"
fi
if [ "${nl}" = "
" ]; then
    printf "test 4:    newline preserved\n"
else
    printf "test 4:    newline not preserved\n"
fi
