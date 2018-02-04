#include "buffer_queue.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h> 
#include <math.h> 
#include <unistd.h>
#include <assert.h>

int randint(int l,int u)
{
    return  floor(l + (1.0*rand()/RAND_MAX)*(u - l + 1 ));
}

const char *song_name =
    "Song Name:Happen To Meet You."
    ;

const char *song_content =
    "We're sobbing" //我們哭了
    "We're laughing" //我們笑著
    "We look up to the sky to find that the stars are still shining" //我們抬頭望天空 星星還亮著幾顆
    "We're singing" //我們唱著
    "The song of time" //時間的歌
    "Then we understand why we hug each other" //才懂得相互擁抱 到底是為了什麼
    "Because I happen to have met you" //因為我剛好遇見你
    "Which is why leaving my footprints is beautiful" //留下足跡才美麗
    "Wind blows; flowers fall; Tears drops like rain" //風吹花落淚如雨
    "Because I don't want to be apart from you" //因為不想分離
    "Because I happen to have met you" //因為剛好遇見你
    "And leave a 10-year-wish" //留下十年的期許
    "I think I would remember you if we meet again" //如果再相遇 我想我會記得你
    "We're sobbing" //我們哭了
    "We're laughing" //我們笑著
    "We look up to the sky to find that the stars are still shining" //我們抬頭望天空 星星還亮著幾顆
    "We're singing" //我們唱著
    "The song of time" //時間的歌
    "Then we understand why we hug each other" //才懂得相互擁抱 到底是為了什麼
    ;

int song_name_len = 0;
int song_content_len = 0;

void*
write_func(void* arg) {
    struct buffer_queue*q = arg;
    int buf_len = 100;
    int add_title_done = 0;
    char *buffer = malloc( (buf_len + 1)* sizeof(char));
    buffer_queue_freeze(q, 1);
    buffer_queue_expand(q, buf_len);
    printf("expand queue %d size\n",buf_len);
    int idx = 0;
    while (idx < song_content_len) {
        int len = randint(1, buf_len);
        if (idx + len > song_content_len) {
            len = song_content_len - idx;
        }
        if (add_title_done) {
            buffer_queue_freeze(q, 1);
        }
        int ret = buffer_queue_add(q, song_content+idx, len);
        assert( ret == 0 );
        memcpy(buffer, song_content+idx, len);
        buffer[len] = '\0';
        printf("\033[0;31mwrite [%d]suffix buff: %s\033[0m\n", len, buffer);
        if (idx < buf_len && idx+len >= buf_len) {
            buffer_queue_prepend(q, song_name, song_name_len, 1);
            printf("\033[0;31mwrite [%d]prefix buff: %s\033[0m\n", song_name_len, song_name);
            add_title_done = 1;
        }
        buffer_queue_stat(q);
        idx += len;
        
        if (add_title_done) {
            buffer_queue_unfreeze(q, 1);
            usleep(200);
        }
    }
    free(buffer);
    printf("write %d bytes done!\n", idx + song_name_len);
    return NULL;
}

void*
read_func(void* arg) {
    struct buffer_queue*q = arg;
    int song_len = song_name_len + song_content_len;
    int total_len = 0;
    char *buffer = malloc((song_len + 1)* sizeof(char));
    while (total_len < song_len) {
        int n = buffer_queue_remove(q, buffer, -1);
        if (n < 0) {
            printf("\033[0;34mread freeze!!\033[0m\n");
            usleep(100);
        }else if (n == 0) {
            printf("\033[0;34mread empty!!\033[0m\n");
            usleep(400);
        }
        else {
            buffer[n] = '\0';
            printf("\033[0;34mread [%d]buff: %s\033[0m\n", n, buffer);
            buffer_queue_stat(q);
            total_len += n;
        }
    }
    free(buffer);
    assert(buffer_queue_get_length(q) == 0);
    printf("read %d bytes done!\n", total_len);
    return NULL;  
}

int main() {
    song_name_len = strlen(song_name);
    song_content_len = strlen(song_content);
    printf("---------test start-------!\n");
    srand((unsigned)time(NULL));

    struct buffer_queue*q = buffer_queue_new();

    pthread_t read_thrd,write_thrd;
    if (pthread_create(&read_thrd, NULL, read_func, q)) {
        fprintf(stderr, "create read thread error\n");
        exit(1);
    }

    if (pthread_create(&write_thrd, NULL, write_func, q)) {
        fprintf(stderr, "create write thread error\n");
        exit(1);
    }
    
    pthread_join(read_thrd, NULL);  
    pthread_join(write_thrd, NULL);
    buffer_queue_free(q);
    printf("---------test end-------!\n");
    return 0;
}