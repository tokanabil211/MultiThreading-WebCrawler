#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#define MAX_URL_LENGTH 1024
#define MAX_VISITED_URLS 10000
#define MAX_THREADS 10
#define BUFFER_SIZE 1000000
#define CRAWL_DELAY_SECONDS 5 // Adjust this value as needed

typedef struct {
    char urls[MAX_VISITED_URLS][MAX_URL_LENGTH];
    int count;
    pthread_mutex_t lock;
} Visited;

Visited urls_visited = { .count = 0, .lock = PTHREAD_MUTEX_INITIALIZER };
int stop_requested = 0;

void handle_signal(int signal) {
    printf("\nCrawler stopped.\n");
    stop_requested = 1;
}

void save_state() {
    FILE *file = fopen("crawler_state.dat", "wb");
    if (file) {
        pthread_mutex_lock(&urls_visited.lock);
        fwrite(&urls_visited, sizeof(Visited), 1, file);
        pthread_mutex_unlock(&urls_visited.lock);
        fclose(file);
    }
}

void load_state(int *is_continuing) {
    FILE *file = fopen("crawler_state.dat", "rb");
    if (file) {
        fread(&urls_visited, sizeof(Visited), 1, file);
        fclose(file);
        *is_continuing = 1;
    } else {
        *is_continuing = 0;
    }
}

void add_url(const char *url) {
    pthread_mutex_lock(&urls_visited.lock);
    if (urls_visited.count < MAX_VISITED_URLS) {
        strcpy(urls_visited.urls[urls_visited.count], url);
        urls_visited.count++;
    }
    pthread_mutex_unlock(&urls_visited.lock);
}

size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    strncat((char *)userp, (char *)contents, total_size);
    return total_size;
}

void extract_text(const char *html, char *text, size_t max_len) {
    size_t i = 0, j = 0;
    int in_tag = 0;
    int in_script = 0;
    int in_style = 0;
    while (html[i] != '\0' && j < max_len - 1) {
        if (html[i] == '<') {
            in_tag = 1;
            if (strncmp(&html[i], "<script", 7) == 0) {
                in_script = 1;
            } else if (strncmp(&html[i], "</script>", 9) == 0) {
                in_script = 0;
            } else if (strncmp(&html[i], "<style", 6) == 0) {
                in_style = 1;
            } else if (strncmp(&html[i], "</style>", 8) == 0) {
                in_style = 0;
            }
        } else if (html[i] == '>') {
            in_tag = 0;
        } else if (!in_tag && !in_script && !in_style) {
            text[j++] = html[i];
        }
        i++;
    }
    text[j] = '\0';
}

unsigned long hash_url(const char *url) {
    unsigned long hash = 5381;
    int c;
    while ((c = *url++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

void save_text_to_file(const char *url, const char *text_content) {
    char filename[MAX_URL_LENGTH];
    snprintf(filename, sizeof(filename), "output_%lu.txt", hash_url(url));
    FILE *file = fopen(filename, "a"); // Open file in append mode
    if (file) {
        fprintf(file, "URL: %s\n\n", url);
        fprintf(file, "Extracted Text:\n%s\n\n", text_content);
        fclose(file);
    }
}

void classify_content(const char *text, char *category, size_t max_len) {
    char lowercase_text[BUFFER_SIZE];
    strcpy(lowercase_text, text);
    for (size_t i = 0; lowercase_text[i]; i++) {
        lowercase_text[i] = tolower(lowercase_text[i]);
    }

    if (strstr(lowercase_text, "technology") != NULL || strstr(lowercase_text, "Technology") != NULL) {
        strncpy(category, "Technology", max_len);
    } else if (strstr(lowercase_text, "news") != NULL || strstr(lowercase_text, "News") != NULL) {
        strncpy(category, "News", max_len);
    } else if (strstr(lowercase_text, "sports") != NULL || strstr(lowercase_text, "Sports") != NULL || strstr(lowercase_text, "Sport") != NULL) {
        strncpy(category, "Sports", max_len);
    } else {
        strncpy(category, "Other", max_len);
    }
}

void *crawler(void *arg) {
    CURL *curl;
    CURLcode res;
    char *url_str = (char *)arg;
    char html_content[BUFFER_SIZE] = {0};
    char text_content[BUFFER_SIZE / 10] = {0};
    char category[50] = {0};

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url_str);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, html_content);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("Downloaded: %s\n", url_str);
            extract_text(html_content, text_content, sizeof(text_content));
            printf("HTML Content for URL %s:\n%s\n", url_str, html_content);
            save_text_to_file(url_str, text_content);
            classify_content(text_content, category, sizeof(category));
            printf("Classified: %s as %s\n", url_str, category);
        }
        curl_easy_cleanup(curl);
    }
    free(url_str);
    pthread_exit(NULL);
}

int main() {
    signal(SIGINT, handle_signal);

    pthread_t threads[MAX_THREADS];
    const char *start_urls[] = {
        "https://www.bbc.com/sport/football",
        "https://www.eurosport.com/score-center.shtml",
        "https://store.steampowered.com/app/2667160/Timeworks/",
        "https://www.skysports.com/football",
    };

    curl_global_init(CURL_GLOBAL_DEFAULT);
    int is_continuing = 0;
    load_state(&is_continuing);

    if (is_continuing) {
        printf("Crawler continuing...\n");
    } else {
        printf("Crawler started...\n");
    }

    int i;
    for (i = 0; i < sizeof(start_urls) / sizeof(start_urls[0]); i++) {
        char *url_copy = malloc(MAX_URL_LENGTH);
        if (url_copy == NULL) {
            fprintf(stderr, "Failed to allocate memory for URL\n");
            exit(EXIT_FAILURE);
        }
        strcpy(url_copy, start_urls[i]);
        pthread_create(&threads[i], NULL, crawler, (void *)url_copy);
        sleep(CRAWL_DELAY_SECONDS); // Introduce a delay between thread creations

        if (stop_requested) {
            save_state();
            break;
        }
    }

    int num_threads = i;

    for (i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    if (stop_requested) {
        // If stop is requested, don't proceed with further crawling
        curl_global_cleanup();
        return 0;
    }

    // Continue crawling with remaining URLs
    for (; i < sizeof(start_urls) / sizeof(start_urls[0]); i++) {
        char *url_copy = malloc(MAX_URL_LENGTH);
        if (url_copy == NULL) {
            fprintf(stderr, "Failed to allocate memory for URL\n");
            exit(EXIT_FAILURE);
        }
        strcpy(url_copy, start_urls[i]);
        pthread_create(&threads[i], NULL, crawler, (void *)url_copy);
        sleep(CRAWL_DELAY_SECONDS); // Introduce a delay between thread creations
    }

    for (i = num_threads; i < MAX_THREADS && i < sizeof(start_urls) / sizeof(start_urls[0]); i++) {
        pthread_join(threads[i], NULL);
    }

    // Cleanup and exit
    curl_global_cleanup();
    return 0;
}