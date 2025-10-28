https://pai-kuan.notion.site/HW2-24ed3aba0aea80f2b7e4e0570c40bc34
# game server to data server
`{
  "type": "User | Room | GameLog",
  "action": "create | query | update | delete | search"(query 只用於登入，search回復符合的)
}`

all update are done with id;
query are also with id.
example:
- `{"action":"create","type":"user","data":"{user json}"}`
- `{"action":"query","type":"user","name":"john"}`//for user login
- `{"action":"search","type":"user/room"}`//for listing out all the online user
- `{"action":"update","type":"user","data":"{user json}"}`
- `{"action":"delete","type":"room","key":"{id}"}`

respond with
- create:`{"response":"success/failed","request":"request, only if failed"}`
- query:`{"response":"success/failed","data":""}`
- update:`{"response":"success/failed","request":"request, only if failed"}`
- search:`{"response":"success/failed","data":[datas]}`
- delete:`{"response":"success/failed","request":"request, only if failed"}`

# server to client

# client to server

# json format
- `User`：`{ id, name, last_login, status("idle"|"playing"|"offline")}`
- `Room`：`{ id, name, hostUserId, visibility("public"|"private"), inviteList[Ids], status("idle"|"playing")}`
- `GameLog`（對局摘要與結果）：`{ id, matchId, roomId, users:[userId], startAt, endAt, results:[{userId, score, lines, maxCombo}] }`