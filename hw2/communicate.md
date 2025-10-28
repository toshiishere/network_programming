https://pai-kuan.notion.site/HW2-24ed3aba0aea80f2b7e4e0570c40bc34
# game server to data server
`{
  "collection": "User | Room | GameLog",
  "action": "create | read | update | delete "
}`

all update are done with id;
read are also with id.


example:`{ "action":"CREATE", "userId":17, "seq":102, "ts":1234567890, "action":"CW" }`
# server to client

# client to server

# json format
- `User`：`{ id, name, log_cnt }`
- `Room`：`{ id, name, hostUserId, visibility("public"|"private"), inviteList[Ids], status("idle"|"playing")}`
- `GameLog`（對局摘要與結果）：`{ id, matchId, roomId, users:[userId], startAt, endAt, results:[{userId, score, lines, maxCombo}] }`