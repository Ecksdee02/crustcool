# Makefile for crustcool.cc 

# common directories
CDIR = c
ODIR = o
# local directories
LOCCDIR = c
LOCODIR = o

# compiler
#CC = xcrun c++
##CC = clang 
CC=c++
#CC=icpc
#FORTRAN=ifort
FORTRAN=gfortran -m64 -O3
#FORTRAN=gfortran -m64 -O3
CFLAGS = -O3 -pipe -I/usr/local/include
#CFLAGS = -lm -parallel -fast 

# main code
OBJS = $(LOCODIR)/crustcool.o $(LOCODIR)/crust.o $(ODIR)/root.o $(ODIR)/vector.o $(ODIR)/odeint.o $(ODIR)/eos.o $(ODIR)/spline.o $(LOCODIR)/condegin19.o $(LOCODIR)/eosmag22.o $(LOCODIR)/eos22.o $(LOCODIR)/timer.o $(LOCODIR)/data.o $(LOCODIR)/ns.o
OBJS3 = $(LOCODIR)/makegrid2.o $(ODIR)/root.o $(ODIR)/vector.o $(ODIR)/odeint.o $(ODIR)/eos.o $(ODIR)/spline.o $(LOCODIR)/condegin19.o $(LOCODIR)/eosmag22.o $(LOCODIR)/eos22.o $(LOCODIR)/envelope2.o

crustcool : $(OBJS)
	$(CC) -o crustcool $(OBJS) $(CFLAGS) -lm -lgfortran -lgsl -lgslcblas -L/Applications/mesasdk/lib -L/usr/local/lib

$(LOCODIR)/crustcool.o : $(LOCCDIR)/crustcool.cc
	$(CC) -c $(LOCCDIR)/crustcool.cc -o $(LOCODIR)/crustcool.o $(CFLAGS) 

makegrid2 : $(OBJS3)
	$(CC) -o makegrid2 $(OBJS3) $(CFLAGS) -lm -lgfortran -lgsl -lgslcblas -L/Applications/mesasdk/lib -L/usr/local/lib

$(LOCODIR)/makegrid2.o : $(LOCCDIR)/makegrid2.cc
	$(CC) -c $(LOCCDIR)/makegrid2.cc -o $(LOCODIR)/makegrid2.o $(CFLAGS) 

$(LOCODIR)/condegin19.o : $(LOCCDIR)/condegin19.f
	$(FORTRAN) -c $(LOCCDIR)/condegin19.f -o $(LOCODIR)/condegin19.o

$(LOCODIR)/eosmag22.o : $(LOCCDIR)/eosmag22.f
	$(FORTRAN) -c $(LOCCDIR)/eosmag22.f -o $(LOCODIR)/eosmag22.o

$(LOCODIR)/eos22.o : $(LOCCDIR)/eos22.f
	$(FORTRAN) -c $(LOCCDIR)/eos22.f -o $(LOCODIR)/eos22.o

$(LOCODIR)/eosmag22.o : $(LOCCDIR)/eosmag22.f
	$(FORTRAN) -c $(LOCCDIR)/eosmag22.f -o $(LOCODIR)/eosmag22.o

$(LOCODIR)/eos22.o : $(LOCCDIR)/eos22.f
	$(FORTRAN) -c $(LOCCDIR)/eos22.f -o $(LOCODIR)/eos22.o


# compile routines from the common directory

$(ODIR)/root.o : $(CDIR)/root.c
	$(CC) -c $(CDIR)/root.c -o $(ODIR)/root.o $(CFLAGS)

$(ODIR)/timer.o : $(CDIR)/timer.c
		$(CC) -c $(CDIR)/timer.c -o $(ODIR)/timer.o $(CFLAGS)

$(ODIR)/data.o : $(CDIR)/data.cc
		$(CC) -c $(CDIR)/data.cc -o $(ODIR)/data.o $(CFLAGS)

$(ODIR)/ns.o : $(CDIR)/ns.c
		$(CC) -c $(CDIR)/ns.c -o $(ODIR)/ns.o $(CFLAGS)

$(ODIR)/eos.o : $(CDIR)/eos.cc
	$(CC) -c $(CDIR)/eos.cc -o $(ODIR)/eos.o $(CFLAGS)

$(ODIR)/envelope2.o : $(CDIR)/envelope2.cc
	$(CC) -c $(CDIR)/envelope2.cc -o $(ODIR)/envelope2.o $(CFLAGS)

$(ODIR)/crust.o : $(CDIR)/crust.cc
	$(CC) -c $(CDIR)/crust.cc -o $(ODIR)/crust.o $(CFLAGS)

$(ODIR)/vector.o : $(CDIR)/vector.cc
	$(CC) -c $(CDIR)/vector.cc -o $(ODIR)/vector.o $(CFLAGS)

$(ODIR)/odeint.o : $(CDIR)/odeint.cc
	$(CC) -c $(CDIR)/odeint.cc -o $(ODIR)/odeint.o $(CFLAGS)

$(ODIR)/spline.o : $(CDIR)/spline.cc
	$(CC) -c $(CDIR)/spline.cc -o $(ODIR)/spline.o $(CFLAGS)

# clean up

clean:
	rm -f $(LOCODIR)/*.o

cleanprecalc:
	rm -f gon_out/precalc*

movie:
#	ffmpeg -qscale 1 -r 20 -b 9600 
	ffmpeg -i png/%3d.png movie.mp4

cleanpng:
	rm -f png/*.png
