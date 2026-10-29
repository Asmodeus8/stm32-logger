CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined -fno-sanitize-recover=all
SRC = ../lib/bme280.c ../lib/lm35.c ../lib/civil_time.c ../lib/record.c test_lib.c

test: $(SRC)
	$(CC) $(CFLAGS) -I../lib -o test_lib $(SRC) -lm
	./test_lib

clean:
	rm -f test_lib emit_frame

interop: ../lib/record.c ../lib/civil_time.c emit_frame.c
	$(CC) -std=c11 -Wall -Werror -I../lib -o emit_frame $^
	./emit_frame | python3 -c "import sys; sys.path.insert(0, '../esp32'); import frames; f = frames.parse_log(sys.stdin.read()); assert f and f['seq'] == 42 and f['metrics']['bme280']['temp_c'] == -3.05, f; print('C frame parsed by the ESP32 bridge:', f)"
