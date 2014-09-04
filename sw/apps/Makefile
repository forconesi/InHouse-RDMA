driver_dir	:= ../driver
DRIVER_DIR	:= $(realpath $(driver_dir))
DRIVER_HEADERS	:= nf10_lbuf.h nf10_user.h
PROGRAM		:= lbuf_rx
OBJS		:= lbuf_rx.o

all: get_driver_header $(PROGRAM)

nf10_lbuf.h:
	ln -sf $(DRIVER_DIR)/$@

nf10_user.h:
	ln -sf $(DRIVER_DIR)/$@

get_driver_header: $(DRIVER_HEADERS)

lbuf_rx.o: $(DRIVER_HEADERS)

$(PROGRAM): $(OBJS)
	gcc -o lbuf_rx $(OBJS)

clean:
	rm -f $(PROGRAM) $(OBJS) $(DRIVER_HEADERS)
