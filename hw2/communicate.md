https://pai-kuan.notion.site/HW2-24ed3aba0aea80f2b7e4e0570c40bc34
# game server to data server communication
`{
  "type": "User | Room | GameLog",
  "action": "create | query | update | delete | search"(query 只用於登入，search回復符合的)
}`

all update are done with id;
query are also with id.
example:
- `{"action":"create","type":"user","data":"{user json}"}`
- `{"action":"query","type":"user","name":"john"}`//for user login
- `{"action":"search","type":"user/room"}`//for listing out all the online user and available room
- `{"action":"update","type":"user","data":"{user json}"}`
- `{"action":"delete","type":"room","data":"room name"}`

respond with
- create:`{"response":"success/failed","data":"request, only if failed"}`
- query:`{"response":"success/failed","data":""}` //data include reason if failed
- update:`{"response":"success/failed","data":"request, only if failed"}`
- search:`{"response":"success/failed","data":[datas]}`
- delete:`{"response":"success/failed","data":"request, only if failed"}`

# client server communication

- login:`{"action":"login/register","name":"name","password":"password"}`
- lobby:`{"action":"create/curroom/curinvite/join/invite","room":"roomname"}`

- response of login:`{"response":"success"}` or `{"response":"failed","reason":"wrong passwd/dulplicate user"}`
- response of lobby:`{"response":"success/failed","reason":"no such room/user / dulplicate room name"}`

# json format
- `User`：`{ name, password, last_login, status("idle"|"playing"|"offline")}`
- `Room`：`{ name, hostUserId, visibility("public"|"private"), inviteList[user Ids], status("idle"|"playing")}`
- `GameLog`（對局摘要與結果）：`{ id, matchId, roomId, users:[userId], startAt, endAt, results:[{userId, score, lines, maxCombo}] }`