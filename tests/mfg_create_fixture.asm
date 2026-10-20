EXTERN CreateBody:PROC
.code
ALIGN 16
DB 0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh
CreateFixture PROC
DB 040h,053h,055h,056h,041h,056h
sub rsp,28h
call CreateBody
add rsp,28h
pop r14
pop rsi
pop rbp
pop rbx
ret
CreateFixture ENDP
ALIGN 16
DB 0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh
CreateRaceFixture PROC
DB 040h,053h,055h,056h,041h,056h
sub rsp,28h
call CreateBody
add rsp,28h
pop r14
pop rsi
pop rbp
pop rbx
ret
CreateRaceFixture ENDP
ALIGN 16
DB 0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh,0CCh
CreateLateRaceFixture PROC
DB 040h,053h,055h,056h,041h,056h
sub rsp,28h
call CreateBody
add rsp,28h
pop r14
pop rsi
pop rbp
pop rbx
ret
CreateLateRaceFixture ENDP
ForeignFixture PROC
mov eax,99
ret
ForeignFixture ENDP
END
