#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<pulse/simple.h>
#include<pulse/error.h>
#include<math.h>
#include<gtk/gtk.h>
#include<gdk/gdk.h>
#include<cairo.h>

int k_from_freq(double rate, double channels, double s_c, double freq) {
	return round(freq*s_c/(rate*channels));
}

void calculate_dft_short_uint8_t(uint8_t *data, double *out, int data_len, int max_k, int le) {
	
	int s_c = data_len/2;
	int lim_k = max_k;
	if(max_k > (s_c)/2)
		lim_k = s_c/2;
	
	int e1 = 0, e2 = 0;
	if(le) {
		e2 = 1;
	} else {
		e1 = 1;
	}
	
	double sum_r, sum_i;
	short temp = 0;
	uint8_t *p = (uint8_t*) &temp;
	for(int k = 0; k < lim_k; k++) {
		sum_r = 0;
		sum_i = 0;
		for(int n = 0; n < s_c; n++) {
			
			p[0] = data[n*2 + e1];
			p[1] = data[n*2 + e2];
			double val = temp/(327.67 * 1.25);
			double a = (2 * M_PI * k * n)/s_c;
			sum_r += val * cos(a);
			sum_i -= val * sin(a);
			//printf("dft: k=%d, n=%d, sum_r=%f, sum_i %f\n", k, n, sum_r, sum_i);
			
		}
		out[k] = sqrt(sum_r*sum_r + sum_i*sum_i);
	}
}

/*double calculate_dft(double freq, double samp_freq, double *data, double max_val, int data_c) {
	
	if(freq > samp_freq/2)
		return -1;
	
	double a = -2 * M_PI * freq / data_c;
	double sum_r, sum_i;
	for(int i = 0; i < data_c; i++) {
		sum_r += data[i] * cos(k * i);
		sum_i -= data[i] * sin(k * i);
		
	}
	
	return sqrt(sum_r*sum_r + sum_i*sum_i)*2/ (data_c);
	
}*/


GtkWidget *image;
pthread_mutex_t *buf_lock;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
uint8_t *buf;
int skip = 5;
int q_len = 512;
int max_k;
double g = 20;
double *q, *q_stable, *q_stable_v;
int run = 1;

int thread_c = 0;
void bp() {}

void *draw_queue() {
	
	thread_c++;
	struct timespec q_t = {
		.tv_nsec = 32000000
	};
	
	pthread_mutex_lock(&mutex);
	while(run) {
		nanosleep(&q_t, NULL);
		gtk_widget_queue_draw(image);
		pthread_cond_wait(&cond, &mutex);
	}
	pthread_mutex_unlock(&mutex);
	thread_c--;
	pthread_exit(NULL);
}

void *fill_buf() {
	thread_c++;
	int i;
	int err;
	
	pa_simple *ct_h;
	pa_sample_spec ss = {
		
		.format = PA_SAMPLE_S16LE,
		.rate = 44100,
		.channels = 2
		
	};
	
	if(!(ct_h = pa_simple_new(NULL, "CT stream", PA_STREAM_RECORD,
					"alsa_output.pci-0000_00_1b.0.analog-stereo.monitor", "CT test", &ss, NULL, NULL, &err)))
	{
		fprintf(stderr, "Failed init CT: %s\n", pa_strerror(err));
		exit(1);
	}
	
	while(run) {
		
		if(pa_simple_read(ct_h, buf, q_len, &err) < 0) {
			printf("Error reading: %s\n", pa_strerror(err));
			exit(1);
		}
		i++;
		if(i == skip) {
			pthread_mutex_lock(buf_lock);
			calculate_dft_short_uint8_t(buf, q, q_len, max_k, 1);
			pthread_mutex_unlock(buf_lock);
			i = 0;
		}
	}
	
	pa_simple_free(ct_h);
	thread_c--;
	pthread_exit(NULL);
	
}

static gboolean delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
	
	run = 0;
	while (thread_c) {
		pthread_cond_signal(&cond);
	}
	gtk_main_quit();
	return TRUE;
	
}

int width = 0;
static gboolean draw_callback(GtkWidget *w, cairo_t *cr, gpointer data) {
	
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, 0, 0, width, 200);
	cairo_fill(cr);
	
	pthread_mutex_lock(buf_lock);
	cairo_set_source_rgb(cr, 1, 0, 0);
	for(int i = 0; i < max_k; i++) {
		q_stable_v[i] = q_stable_v[i] + g;
		q_stable[i] = q_stable[i] - q_stable_v[i];
		if(q[i] > q_stable[i]) {
			q_stable[i] = q[i];
			q_stable_v[i] = 0;
		}
		cairo_rectangle(cr, i*12, 200, 10, -q_stable[i]/60);
		cairo_fill(cr);
		
	}
	pthread_mutex_unlock(buf_lock);
	pthread_cond_signal(&cond);
	return TRUE;
}

int main(int argc, char *argv[]) {
	
	max_k = k_from_freq(44100, 2, q_len/2, 6000);
	width = 10 * max_k + 2*(max_k-1);
	
	int q_size = q_len/2;
	q = malloc(q_size*sizeof(double));
	q_stable = malloc(q_size*sizeof(double));
	q_stable_v = malloc(q_size*sizeof(double));
	buf_lock = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(buf_lock, NULL);
	
	buf = malloc(sizeof(uint8_t) * q_len);
	
	gtk_init(&argc, &argv);
	
	GtkWidget *window;
	
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), width, 200);
	
	printf("Drawn\n");
	
	image = gtk_drawing_area_new();
	//gtk_drawing_area_size(GTK_DRAWING_AREA(image), 600, 400);
	
	g_signal_connect(window, "delete-event", G_CALLBACK(delete), NULL);
	g_signal_connect(image, "draw", G_CALLBACK(draw_callback), NULL);
	//g_signal_connect(image, "expose-event", G_CALLBACK(expose), NULL);
	//g_signal_connect(image, "configure-event", G_CALLBACK(configure), NULL);
	
	run = 1;
	pthread_t *buf_t = malloc(sizeof(pthread_t));
	pthread_create(buf_t, NULL, fill_buf, NULL);
	
	pthread_t *dft_t = malloc(sizeof(pthread_t));
	pthread_create(dft_t, NULL, draw_queue, NULL);
	
	gtk_container_add(GTK_CONTAINER(window), image);
	printf("Add\n");
	gtk_widget_show(image);
	gtk_widget_show(window);
	
	printf("Show\n");
	
	gtk_main();
	
	pthread_join(*dft_t, NULL);
	pthread_join(*buf_t, NULL);
	free(dft_t);
	free(buf_t);
	free(q);
	free(q_stable);
	free(q_stable_v);
	free(buf_lock);
	exit(1);
	
}