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
- lobby:`{"action":"create/curroom/curinvite/join","roomname":"john"}`
    - if`{create}`, add attribute of `{visibility}`
- room:`{"action":invite/start}`
    - invite:`{"action":"invite","name":"john"}`
- game:`{"action":"ready", "name":abc}` to make sure it is connected and for distinguishing
  

- response of login:`{"response":"success"}` or `{"response":"failed","reason":"wrong passwd/dulplicate user"}`
- response of lobby: reason only exist if failed
    - create:`{"response":"success/failed","reason":"dulplicate room"}`
    - curroom:`{"response":"success/failed","data":[rooms]}`
    - curinvite:`{"response":"success/failed","data":[rooms]}`
    - join:`{"response":"success/failed","reason":"no such room"}`
- response of room op:
    - invite:`{"response":"success/failed","reason":"no such user"}`
    - start:`{"response":"success/failed"}`
        - to tell client to start the game:`{"action":"start","data":room}`(room info include port, which is 50000+id), also tell the oppoUser

# json format
- `User`：`{ id, name, password, last_login, status("idle"|"playing"|"offline"), roomName}`
- `Room`：`{ id, name, hostUser, visibility("public"|"private"), inviteList[user Ids], status("idle"|"playing"|"room"), oppoUser}`
- `GameLog`（對局摘要與結果）：`{ roomId, users:[userId], startAt, endAt, results:[{ score, lines, maxCombo}] }`
- `gamestate`:`{"tick":#, "p1":"string_of_tetris_state", "p2":"string_of_tetirs_state"}`