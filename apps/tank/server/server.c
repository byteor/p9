/*
 * server.c
 *
 * Copyright (c) Zhou Peng <lockrecv@qq.com>
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>

#include "server.h"
#include "logger.h"
#include "map.h"
#include "link.h"
#include "cJSON.h"
#include "list.h"

/*
 * server command line arguments
 */
static const char *server_port    = "6000";
static const char *server_map     = "map.txt";
static const char *server_timeout = "400";
static const char *server_log     = "srv.log";

static struct option server_options[] = {
	{"port",    required_argument, 0, 'p'},
	{"map",     required_argument, 0, 'm'},
	{"timeout", required_argument, 0, 't'},
	{"stdout",  no_argument,       0, 's'},
	{0,         0,                 0,   0}
};

/*
 * this game
 */
static struct game game;

static void game_start(void)
{
	logger_error("%s\n", "server: game start.");
	cJSON *root, *body;
	char *msg;
	size_t len;
	short tid;

	/*
	 * notify each player game start and their unique IDs.
	 */
	for (tid = 0; tid < TEAM_MAX; tid++) {
		root = cJSON_CreateObject();
		cJSON_AddItemToObject(root, "head", cJSON_CreateString("game start"));
		cJSON_AddItemToObject(root, "body", body=cJSON_CreateObject());
		cJSON_AddNumberToObject(body, "id", game.teams[tid].id);
		msg = cJSON_PrintUnformatted(root);
		len = strlen(msg);
		if (write(game.teams[tid].sockfd, msg, len) < 0)
			logger_error("team %hi, broken socket.\n", game.teams[tid].id);
		cJSON_Delete(root);
		free(msg);
	}

	/*
	 * all player register their names
	 */
	char buf[BUFFER_MAX+1] = {'\0'};
	for (tid = 0; tid < TEAM_MAX; tid++) {
		if (read(game.teams[tid].sockfd, buf, BUFFER_MAX) < 0) {
			logger_error("team %hi, broken socket.\n", game.teams[tid].id);
			continue;
		}
		cJSON *json_reg = cJSON_Parse(buf);
		cJSON *json_id = cJSON_GetObjectItem(json_reg, "id");
		cJSON *json_name = cJSON_GetObjectItem(json_reg, "name");
		if (json_id->type == cJSON_Number &&
		    json_id->valueint == game.teams[tid].id &&
		    json_name->type == cJSON_String) {
			strncpy(game.teams[tid].name, json_name->valuestring, NAME_MAX);
		} else {
			logger_warn("team %hi, evil player will be punished.\n", game.teams[tid].id);
			close(game.teams[tid].sockfd);
			game.teams[tid].sockfd = -1;
		}
	}
}

static void game_over(void)
{
	logger_error("%s\n", "server: game over.");
	cJSON *root, *body;
	char *msg;
	size_t len;
	short tid;

	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "head", cJSON_CreateString("game over"));
	cJSON_AddItemToObject(root, "body", body=cJSON_CreateObject());
	cJSON_AddItemToObject(body, "message", cJSON_CreateString("Thanks to play."));
	msg = cJSON_PrintUnformatted(root);
	len = strlen(msg);

	for (tid = 0; tid < TEAM_MAX; tid++) {
		if (write(game.teams[tid].sockfd, msg, len) < 0)
			logger_error("team %hi, broken socket.\n", game.teams[tid].id);
		else
			close(game.teams[tid].sockfd);
		game.teams[tid].sockfd = -1;
	}

	cJSON_Delete(root);
	free(msg);
}

static void leg_start(void)
{
	logger_warn("%s\n", "server: leg start.");
	cJSON *root, *body;
	char *msg;
	size_t len;
	short tid;

	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "head", cJSON_CreateString("leg start"));
	cJSON_AddItemToObject(root, "body", body=cJSON_CreateObject());
	msg = cJSON_PrintUnformatted(root);
	len = strlen(msg);

	for (tid = 0; tid < TEAM_MAX; tid++)
		if (write(game.teams[tid].sockfd, msg, len) < 0)
			logger_error("team %hi, broken socket.\n", game.teams[tid].id);

	cJSON_Delete(root);
	free(msg);
}

static void leg_end(void)
{
	logger_warn("%s\n", "server: leg end.");
	cJSON *root, *body;
	char *msg;
	size_t len;
	short tid;

	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "head", cJSON_CreateString("leg end"));
	cJSON_AddItemToObject(root, "body", body=cJSON_CreateObject());
	msg = cJSON_PrintUnformatted(root);
	len = strlen(msg);

	for (tid = 0; tid < TEAM_MAX; tid++)
		if (write(game.teams[tid].sockfd, msg, len) < 0)
			logger_error("team %hi, broken socket.\n", game.teams[tid].id);

	cJSON_Delete(root);
	free(msg);
}

static cJSON *game_json(void)
{
	cJSON *root, *body;
	short tid;

	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "head", cJSON_CreateString("round step"));
	cJSON_AddItemToObject(root, "body", body=cJSON_CreateObject());
	cJSON_AddNumberToObject(body, "leg", game.leg_remain);
	cJSON_AddNumberToObject(body, "round", game.round_remain);

	cJSON *teams;
	cJSON_AddItemToObject(body, "teams", teams=cJSON_CreateArray());
	for (tid = 0; tid < TEAM_MAX; tid++) {
		cJSON *team = cJSON_CreateObject();
		cJSON_AddNumberToObject(team, "id", game.teams[tid].id);
		cJSON_AddNumberToObject(team, "life_remain", game.teams[tid].life_remain);

		cJSON *stars;
		cJSON_AddItemToObject(team, "stars", stars=cJSON_CreateArray());
		struct star *star;
		list_for_each_entry(star, &game.teams[tid].stars, list) {
			cJSON *starjson = cJSON_CreateObject();
			cJSON_AddItemToObject(starjson, "dir", cJSON_CreateString(dir2str(star->dir)));
			cJSON_AddNumberToObject(starjson, "y", star->pos.y);
			cJSON_AddNumberToObject(starjson, "x", star->pos.x);
			cJSON_AddItemToArray(stars, starjson);
		}

		cJSON *bullets;
		cJSON_AddItemToObject(team, "bullets", bullets=cJSON_CreateArray());
		struct bullet *bullet;
		list_for_each_entry(bullet, &game.teams[tid].bullets, list) {
			cJSON *bulletjson = cJSON_CreateObject();
			cJSON_AddItemToObject(bulletjson, "dir", cJSON_CreateString(dir2str(bullet->dir)));
			cJSON_AddNumberToObject(bulletjson, "y", bullet->pos.y);
			cJSON_AddNumberToObject(bulletjson, "x", bullet->pos.x);
			cJSON_AddItemToArray(bullets, bulletjson);
		}
		cJSON_AddItemToArray(teams, team);
	}
	cJSON_AddItemToObject(body, "map", map_json());
	return root;
}

static int round_step(void)
{
	logger_info("%s\n", "server: round step.");
	char *msg;
	size_t len;
	cJSON *root;
	short tid;

	root = game_json();
	msg = cJSON_PrintUnformatted(root);
	len = strlen(msg);
	for (tid = 0; tid < TEAM_MAX; tid++)
		if (write(game.teams[tid].sockfd, msg, len) < 0)
			logger_error("team %hi, broken socket.\n", game.teams[tid].id);
	cJSON_Delete(root);
	free(msg);

	/* player actions */
	char buf[BUFFER_MAX+1] = {'\0'};
	for (tid = 0; tid < TEAM_MAX; tid++) {
		if (read(game.teams[tid].sockfd, buf, BUFFER_MAX) < 0) {
			logger_error("team %hi, broken socket.\n", game.teams[tid].id);
			continue;
		}
		cJSON *action = cJSON_Parse(buf);
		/*
		 * actions here
		 */
		cJSON_Delete(action);
	}
	return 0;
}

static int team_setup(void)
{
	short tm, tk;

	game.round_remain = ROUND_MAX;

	for (tm = 0; tm < TEAM_MAX; tm++) {
		game.teams[tm].life_remain = LIFE_MAX;
		for (tk = 0; tk < TANK_MAX; tk++) {
			game.teams[tm].tanks[tk].id = tm * TANK_MAX + tk;
			game.teams[tm].tanks[tk].star_count = 0;

			/* tank postion */
			short y, x;
			for (y = 0; y < Y_MAX; y++) {
				for (x = 0; x < X_MAX; x++) {
					if (map_get(y, x) == AREA) {
						game.teams[tm].tanks[tk].pos.y = y;
						game.teams[tm].tanks[tk].pos.x = x;
						map_set(y, x, TANK);
						goto tank_ok;
					}
				}
			}
			logger_warn("%s\n", "map area less than tanks.");
			return -1;
		tank_ok:
			logger_info("team: %hi, tank: %hi, at position (y:%hi, x:%hi)\n",
				    game.teams[tm].id,
				    game.teams[tm].tanks[tk].id,
				    game.teams[tm].tanks[tk].pos.y,
				    game.teams[tm].tanks[tk].pos.x);
		}
		struct list_head *pos, *n;
		list_for_each_safe(pos, n, &game.teams[tm].stars) {
			list_del(pos);
			struct star *star = list_entry(pos, struct star, list);
			free(star);
		}
		list_for_each_safe(pos, n, &game.teams[tm].bullets) {
			list_del(pos);
			struct bullet *bullet = list_entry(pos, struct bullet, list);
			free(bullet);
		}
	}
	return 0;
}

static void game_setup(void)
{
	short tm;

	game.leg_remain = LEG_MAX;

	for (tm = 0; tm < TEAM_MAX; tm++) {
		game.teams[tm].id = tm;
		game.teams[tm].life_remain = LIFE_MAX;
		game.teams[tm].sockfd = link_accept();
		memset(game.teams[tm].name, '\0', NAME_MAX+1);
		INIT_LIST_HEAD(&game.teams[tm].stars);
		INIT_LIST_HEAD(&game.teams[tm].bullets);
	}
}

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	/* server options */
	int c;
	while ((c = getopt_long(argc, argv, "p:m:t:s", server_options, NULL)) != -1) {
		switch (c) {
		case 'p':
			server_port = optarg;
			break;
		case 'm':
			server_map = optarg;
			break;
		case 't':
			server_timeout = optarg;
			break;
		case 's':
			server_log = "/dev/stdout";
			break;
		default:
			exit(-1);
		}
	}

	if (logger_open(server_log) != 0)
		exit(-2);
	if (map_load(server_map) != 0)
		exit(-3);
	if (link_open(server_port) != 0)
		exit(-4);

	/* play game */
	game_setup();
	game_start();
	while (game.leg_remain-- > 0) {
		team_setup();
		leg_start();
		while (game.round_remain-- > 0) {
			if (round_step() != 0)
				break;
		}
		leg_end();
	}
	game_over();

	link_close();
	logger_close();

	return 0;
}