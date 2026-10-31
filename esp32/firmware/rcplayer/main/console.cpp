#include "console.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "board/board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rcstage/stage.h"

namespace console {

static QueueHandle_t sQueue;

static void readerTask(void*) {
    char line[128];
    size_t n = 0;
    while (true) {
        int c = fgetc(stdin);
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (c == '\r') continue;
        if (c != '\n') {
            if (n < sizeof(line) - 1) line[n++] = (char) c;
            continue;
        }
        line[n] = 0;
        n = 0;
        Request r;
        if (!strcmp(line, "snap")) r.cmd = Cmd::Snap;
        else if (!strcmp(line, "next")) r.cmd = Cmd::Next;
        else if (!strcmp(line, "prev")) r.cmd = Cmd::Prev;
        else if (!strcmp(line, "list")) r.cmd = Cmd::List;
        else if (!strcmp(line, "stats")) r.cmd = Cmd::Stats;
        else if (!strcmp(line, "bench")) r.cmd = Cmd::Bench;
        else if (!strcmp(line, "named") || !strncmp(line, "named ", 6)) {
            // The showing document's named variables and their values (those with the prefix).
            std::vector<rcstage::NamedValue> vs;
            const char* prefix = line[5] ? line + 6 : "";
            if (!rcstage::namedValues(vs, 2000)) printf("named: no answer from the player\n");
            for (auto& v : vs)
                if (!strncmp(v.name.c_str(), prefix, strlen(prefix)))
                    printf("%s = %s (%s)\n", v.name.c_str(), v.value.c_str(), v.type.c_str());
            continue;
        }
        else if (!strncmp(line, "act ", 4)) {
            r.cmd = Cmd::Act;
            strlcpy(r.text, line + 4, sizeof(r.text));
        }
        else if (!strncmp(line, "doc ", 4)) { r.cmd = Cmd::Doc; r.arg = atoi(line + 4); }
        else if (!strcmp(line, "res full")) { r.cmd = Cmd::Res; r.arg = 1; }
        else if (!strcmp(line, "res half")) { r.cmd = Cmd::Res; r.arg = 2; }
        else if (board::console(line)) continue;   // board diagnostics: "pmu", "expander", ...
        else if (line[0]) { printf("? unknown command '%s'\n", line); continue; }
        xQueueSend(sQueue, &r, 0);
    }
}

void start() {
    sQueue = xQueueCreate(8, sizeof(Request));
    board::consoleInit();   // blocking, driver-backed stdin/stdout on the board's console port
    xTaskCreatePinnedToCore(readerTask, "console", 4096, nullptr, 3, nullptr, 0);
}

bool poll(Request& out) { return xQueueReceive(sQueue, &out, 0) == pdTRUE; }

}  // namespace console
