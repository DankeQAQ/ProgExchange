#include "pe_exchange.h"

typedef struct
{
    int tid;
    int id;
    char product[20];
    int quantity;
    int price;
    char type;
} Order;

typedef struct
{
    int pid;
    int exchange;
    int trader;
    char n_exchange[SIZE];
    char n_trader[SIZE];
    int live;
} Trader;

int products_num;
int trader_num;
int order_num = 0;
long long total_fee = 0;

char (*product_book)[20];
Trader *trader_book;
Order *order_book;
long long **position_book;

int READ = 0;
int current_trader;
int EXIT = 0;

void read_command(int tid, char *line)
{
    // printf("[PEX] READ COMMAND\n");

    int o_id = 0, quantity = 0, price = 0;
    char product[19] = {'\0'}, type[12] = {'\0'};
    int ret = sscanf(line, "%s %d %s %d %d;", type, &o_id, product, &quantity, &price);
    for (int i = 0;; i++)
    {
        if (line[i] == ';')
        {
            line[i] = '\0';
            break;
        }
    }
    printf("[PEX] [T%d] Parsing command: <%s>\n", tid, line);

    if (ret == 5 && (strcmp(type, "SELL") == 0 || strcmp(type, "BUY") == 0))
    {
        int number = quantity >= 1 && quantity <= 999999 && price >= 1 && price <= 999999;
        int product_in_file = 0;
        for (int i = 0; i < products_num; i++)
        {
            if (strcmp(product_book[i], product) == 0)
            {
                product_in_file = 1;
            }
        }
        int order_increment = 0;
        for (int i = 0; i < order_num; i++)
        {
            if (order_book[i].tid == tid)
            {
                order_increment++;
            }
        }
        char tmp[80] = {'\0'};
        if (number == 0 || product_in_file == 0 || order_increment != o_id)
        {
            memset(tmp, 0, sizeof(tmp));
            sprintf(tmp, "INVALID;");
            write(trader_book[tid].exchange, tmp, strlen(tmp));
            kill(trader_book[tid].pid, SIGUSR1);
        }
        else
        {
            Order order;
            order.id = o_id;
            strncpy(order.product, product, 20);
            order.quantity = quantity;
            order.price = price;
            order.tid = tid;
            order.type = type[0];
            order_book = realloc(order_book, (order_num + 1) * sizeof(Order));
            order_book[order_num] = order;
            order_num++;

            memset(tmp, 0, sizeof(tmp));
            sprintf(tmp, "ACCEPTED %d;", o_id);
            write(trader_book[tid].exchange, tmp, strlen(tmp));
            kill(trader_book[tid].pid, SIGUSR1);

            for (int i = 0; i < trader_num; i++)
            {
                if (i != tid)
                {
                    memset(tmp, 0, sizeof(tmp));
                    sprintf(tmp, "MARKET %s %s %d %d;", type, product, quantity, price);
                    write(trader_book[i].exchange, tmp, strlen(tmp));
                    kill(trader_book[i].pid, SIGUSR1);
                }
            }

            // try match
            for (int i = 0; i < products_num; i++)
            {
                while (1)
                {
                    int buy_max = -1, sell_min = -1, quantity, new_o, old_o, order_price;
                    long long total, f;
                    for (int j = 0; j < order_num; j++)
                    {
                        if (strcmp(product_book[i], order_book[j].product) == 0 && order_book[j].quantity != 0)
                        {
                            if (order_book[j].type == 'B')
                            {
                                if (buy_max == -1 || order_book[j].price > order_book[buy_max].price)
                                {
                                    buy_max = j;
                                }
                            }
                            else if (order_book[j].type == 'S')
                            {
                                if (sell_min == -1 || order_book[j].price < order_book[sell_min].price)
                                {
                                    sell_min = j;
                                }
                            }
                        }
                    }

                    if (buy_max == -1 || sell_min == -1)
                        break;
                    if (order_book[buy_max].price < order_book[sell_min].price)
                        break;
                    if (order_book[buy_max].quantity < order_book[sell_min].quantity)
                    {
                        quantity = order_book[buy_max].quantity;
                    }
                    else
                    {
                        quantity = order_book[sell_min].quantity;
                    }
                    order_book[buy_max].quantity -= quantity;
                    order_book[sell_min].quantity -= quantity;

                    if (buy_max > sell_min)
                    {
                        new_o = buy_max;
                        old_o = sell_min;
                    }
                    else
                    {
                        new_o = sell_min;
                        old_o = buy_max;
                    }

                    order_price = order_book[old_o].price;
                    total = (long long)quantity * (long long)order_price;
                    f = total * FEE_RATE + 0.5;
                    total_fee += f;

                    printf("[PEX] Match: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n", order_book[old_o].id, order_book[old_o].tid, order_book[new_o].id, order_book[new_o].tid, total, f);

                    position_book[order_book[buy_max].tid][2 * i] += quantity;
                    position_book[order_book[buy_max].tid][2 * i + 1] -= total;
                    position_book[order_book[sell_min].tid][2 * i] -= quantity;
                    position_book[order_book[sell_min].tid][2 * i + 1] += total;
                    position_book[order_book[new_o].tid][2 * i + 1] -= f;

                    memset(tmp, 0, sizeof(tmp));
                    sprintf(tmp, "FILL %d %d;", order_book[new_o].id, quantity);
                    write(trader_book[order_book[new_o].tid].exchange, tmp, strlen(tmp));
                    kill(trader_book[order_book[new_o].tid].pid, SIGUSR1);

                    memset(tmp, 0, sizeof(tmp));
                    sprintf(tmp, "FILL %d %d;", order_book[old_o].id, quantity);
                    write(trader_book[order_book[old_o].tid].exchange, tmp, strlen(tmp));
                    kill(trader_book[order_book[old_o].tid].pid, SIGUSR1);
                }
            }

            // print order book
            printf("[PEX]\t--ORDERBOOK--\n");

            for (int p = 0; p < products_num; p++)
            {
                int *sort = malloc(order_num * sizeof(int));
                memset(sort, -1, order_num * sizeof(int));
                int *mask = malloc(order_num * sizeof(int));
                memset(mask, -1, order_num * sizeof(int));
                int n = 0;

                for (int i = 0; i < order_num; i++)
                {
                    int max = -1;
                    for (int j = 0; j < order_num; j++)
                    {
                        if (strcmp(product_book[p], order_book[j].product) == 0 && mask[j] == -1 && order_book[j].quantity != 0)
                        {
                            if (max == -1 || order_book[j].price > order_book[max].price)
                            {
                                max = j;
                            }
                        }
                    }
                    if (max != -1)
                    {
                        sort[n++] = max;
                        mask[max] = 1;
                    }
                }

                int buy = 0, sell = 0;
                for (int i = 0; i < n; i++)
                {
                    if (order_book[sort[i]].type == 'B')
                    {
                        buy++;
                    }
                    else
                    {
                        sell++;
                    }
                }

                int buy_r = 0, sell_r = 0;
                for (int i = 1; i < n; i++)
                {
                    if (order_book[sort[i]].price == order_book[sort[i - 1]].price)
                    {
                        if (order_book[sort[i]].type == 'B')
                        {
                            buy_r++;
                        }
                        else
                        {
                            sell_r++;
                        }
                    }
                }
                buy -= buy_r;
                sell -= sell_r;

                printf("[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", product_book[p], buy, sell);
                for (int i = 0; i < n; i++)
                {
                    int total = order_book[sort[i]].quantity;
                    int count = 1;
                    while (i + 1 < n && (order_book[sort[i]].price == order_book[sort[i + 1]].price))
                    {
                        i++;
                        total += order_book[sort[i]].quantity;
                        count++;
                    }
                    printf("[PEX]\t\t%s %d @ $%d (%d ", order_book[sort[i]].type == 'B' ? "BUY" : "SELL", total, order_book[sort[i]].price, count);
                    if (count == 1)
                    {
                        printf("order)\n");
                    }
                    else
                    {
                        printf("orders)\n");
                    }
                }
                free(mask);
                free(sort);
            }

            // print position book
            printf("[PEX]\t--POSITIONS--\n");
            for (int i = 0; i < trader_num; i++)
            {
                printf("[PEX]\tTrader %d: ", i);
                for (int j = 0; j < products_num; j++)
                {
                    printf("%s %lld ($%lld)", product_book[j], position_book[i][j * 2], position_book[i][j * 2 + 1]);
                    if (j + 1 == products_num)
                    {
                        printf("\n");
                    }
                    else
                    {
                        printf(", ");
                    }
                }
            }
        }
    }
    else if (ret == 2 && strcmp(type, "CANCEL") == 0)
    {
        int cancel_id = -1;
        char tmp[30] = {'\0'};
        memset(tmp, 0, sizeof(tmp));
        for (int i = 0; i < order_num; i++)
        {
            if (order_book[i].tid == tid && order_book[i].id == o_id)
            {
                cancel_id = i;
            }
        }
        if (cancel_id == -1 || order_book[cancel_id].quantity == 0)
        {
            sprintf(tmp, "INVALID;");
        }
        else
        {
            order_book[cancel_id].quantity = 0;
            sprintf(tmp, "CANCELLED %d;", o_id);
        }
        write(trader_book[tid].exchange, tmp, strlen(tmp));
        kill(trader_book[tid].pid, SIGUSR1);
    }
    else if (ret == 4 && strcmp(type, "AMEND") == 0)
    {
        int number = quantity >= 1 && quantity <= 999999 && price >= 1 && price <= 999999;
        int amend_id = -1;
        char tmp[30] = {'\0'};
        memset(tmp, 0, sizeof(tmp));
        for (int i = 0; i < order_num; i++)
        {
            if (order_book[i].tid == tid && order_book[i].id == o_id)
            {
                amend_id = i;
            }
        }
        if (amend_id == -1 || order_book[amend_id].quantity == 0 || number == 0)
        {
            sprintf(tmp, "INVALID;");
        }
        else
        {
            order_book[amend_id].quantity = quantity;
            order_book[amend_id].price = price;
            sprintf(tmp, "AMENDED %d;", o_id);
        }
        write(trader_book[tid].exchange, tmp, strlen(tmp));
        kill(trader_book[tid].pid, SIGUSR1);
    }
    else
    {
        char tmp[30] = {'\0'};
        memset(tmp, 0, sizeof(tmp));
        int len = sprintf(tmp, "INVALID;");
        write(trader_book[tid].exchange, tmp, len);
        kill(trader_book[tid].pid, SIGUSR1);
    }
}

void handler1(int sign, siginfo_t *siginfo, void *context)
{
    pid_t process_id;
    while ((process_id = waitpid(-1, NULL, WNOHANG)) > 0)
    {
        for (int i = 0; i < trader_num; i++)
        {
            if (trader_book[i].pid == process_id)
            {
                printf("[PEX] Trader %d disconnected\n", i);
                trader_book[i].live = 0;
                close(trader_book[i].exchange);
                close(trader_book[i].trader);
                unlink(trader_book[i].n_exchange);
                unlink(trader_book[i].n_trader);
            }
        }
    }

    int finished = 1;
    for (int i = 0; i < trader_num; i++)
    {
        if (trader_book[i].live == 1)
        {
            finished = 0;
        }
    }
    if (finished == 1)
    {
        printf("[PEX] Trading completed\n");
        printf("[PEX] Exchange fees collected: $%lld\n", total_fee);
        free(product_book);
        free(trader_book);
        free(order_book);
        for (int i = 0; i < trader_num; i++)
        {
            free(position_book[i]);
        }
        free(position_book);
        EXIT = 1;
    }
}

void handler2(int sign, siginfo_t *siginfo, void *context)
{
    // printf("[PEX] SIGUSR1\n");
    for (int i = 0; i < trader_num; i++)
    {
        if (trader_book[i].pid == siginfo->si_pid)
            current_trader = i;
    }
    READ = 1;
}

void read_product_book(char *filename)
{
    printf("[PEX] Starting\n");
    FILE *file = fopen(filename, "r");
    fscanf(file, "%d", &products_num);

    int length = products_num * 20;
    product_book = malloc(length * sizeof(char));

    for (int i = 0; i < products_num; i++)
        fscanf(file, "%s", product_book[i]);

    fclose(file);
}

void print_product_book()
{
    printf("[PEX] Trading %d products: ", products_num);
    for (int i = 0; i < products_num; i++)
    {
        printf("%s", product_book[i]);
        if (i != products_num - 1)
        {
            printf(" ");
        }
    }
    printf("\n");
}

void create_pipeline(int n, char *argv[])
{
    trader_num = n;
    trader_book = (Trader *)malloc(trader_num * sizeof(Trader));
    for (int i = 0; i < trader_num; i++)
    {
        char tmp[40];
        sprintf(tmp, FIFO_EXCHANGE, i);
        strcpy(trader_book[i].n_exchange, tmp);
        unlink(tmp);
        mkfifo(tmp, 0666);
        printf("[PEX] Created FIFO %s\n", tmp);

        memset(tmp, 0, 40);
        sprintf(tmp, FIFO_TRADER, i);
        strcpy(trader_book[i].n_trader, tmp);
        unlink(tmp);
        mkfifo(tmp, 0666);
        printf("[PEX] Created FIFO %s\n", tmp);

        pid_t pid = fork();
        trader_book[i].pid = pid;
        if (pid == 0)
        {
            memset(tmp, 0, 40);
            sprintf(tmp, "%d", i);
            execl(argv[i + 2], argv[i + 2], tmp, NULL);
        }
        else
        {
            printf("[PEX] Starting trader %d (%s)\n", i, argv[i + 2]);

            trader_book[i].exchange = open(trader_book[i].n_exchange, O_WRONLY);
            trader_book[i].trader = open(trader_book[i].n_trader, O_RDONLY | O_NONBLOCK);
            printf("[PEX] Connected to %s\n", trader_book[i].n_exchange);
            printf("[PEX] Connected to %s\n", trader_book[i].n_trader);
            trader_book[i].live = 1;
        }
    }
}

void create_position_book()
{
    position_book = (long long **)malloc(trader_num * sizeof(long long *));
    for (int i = 0; i < trader_num; i++)
    {
        position_book[i] = (long long *)malloc(2 * products_num * sizeof(long long));
        memset(position_book[i], 0, 2 * products_num * sizeof(long long));
    }
}

void create_signal()
{
    struct sigaction sa;
    sa.sa_sigaction = handler1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa2;
    sa2.sa_sigaction = handler2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa2, NULL);
}

void open_market()
{
    for (int i = 0; i < trader_num; i++)
    {
        char buffer[SIZE] = "MARKET OPEN;";
        write(trader_book[i].exchange, buffer, strlen(buffer));
        kill(trader_book[i].pid, SIGUSR1);
    }
}

int main(int argc, char *argv[])
{
    read_product_book(argv[1]);
    print_product_book();
    create_pipeline(argc - 2, argv);
    create_position_book();
    create_signal();
    open_market();

    while (1)
    {
        if (READ)
        {
            READ = 0;
            char tmp[SIZE];
            memset(tmp, 0, sizeof(tmp));
            if (read(trader_book[current_trader].trader, tmp, sizeof(tmp)) > 0)
            {
                read_command(current_trader, tmp);
            }
        }
        if (EXIT)
        {
            break;
        }
    }
}
