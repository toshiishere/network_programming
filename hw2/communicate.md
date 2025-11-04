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
- create:`{"response":"success/failed","reason":"request, only if failed","id":#}`
- query:`{"response":"success/failed","data/reason":""}` //data include reason if failed
- update:`{"response":"success/failed","reason":"only if failed"}`
- search:`{"response":"success/failed","data/reason":[datas]}`
- delete:`{"response":"success/failed","reason":"request, only if failed"}`

# client server communication

- login:`{"action":"login/register","name":"name","password":"password"}`
- lobby:`{"action":"create/curroom/curinvite/join/invite","roomname":"john"}`
    - if`{create}`, add attribute of `{visibility}`
  

- response of login:`{"response":"success"}` or `{"response":"failed","reason":"wrong passwd/dulplicate user"}`
- response of lobby: reason only exist if failed
    - create:`{"response":"success/failed","reason":"dulplicate room"}`
    - curroom:`{"response":"success/failed","data":[rooms]}`
    - curinvite:`{"response":"success/failed","data":[rooms]}`
    - join:`{"response":"success/failed","reason":"no such room"}`
    - invite:`{"response":"success/failed","reason":"no such user"}`

# json format
- `User`：`{ id, name, password, last_login, status("idle"|"playing"|"offline"), roomName}`
- `Room`：`{ id, name, hostUser, visibility("public"|"private"), inviteList[user Ids], status("idle"|"playing")}`
- `GameLog`（對局摘要與結果）：`{ id, matchId, roomId, users:[userId], startAt, endAt, results:[{userId, score, lines, maxCombo}] }`