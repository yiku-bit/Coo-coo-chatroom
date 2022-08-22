#include "operations.h"
#include "serverdatacenter.h"
#include "databaseoperation.h"

Operations::Operations(QObject *parent) : QObject(parent) {

}

ServerDataCenter& dcenter = ServerDataCenter::Singleton();
using resp = QList<QJsonObject>;
using Json = QJsonObject;

//登录操作
QList<QJsonObject> Operations::loginResponse(QJsonObject json)
{
    DatabaseOperation & db = DatabaseOperation::Singleton();
    ServerDataCenter& dcenter = ServerDataCenter::Singleton();
    QString username = json["Username"].toString();
    QString password = json["Password"].toString();

    //登录成功/失败信息
    QJsonObject response = {{"MsgType", "LogInConfirm"}, {"IsLegal", false}};
    if (!db.attemptLogIn(username, password)) {
        return {response};
    }
    response["IsLegal"] = true;
    response["Username"] = username;
    QList<QJsonObject> ret = {response};

    //用户相关个人信息
    auto &user = dcenter.getUser(username);
    response["Nickname"] = user.getNickname();
    response["Profile"] = user.getProfile();

    //用户所有会话列表
    auto sessionlist = db.querySessionsByMember(username.toUtf8().data());
    for (int i = 0; i < sessionlist.size(); i++)
    {
        //会话的成员信息
        int ssid = sessionlist.at(i);
        //会话基本信息
        ret.append(dcenter.getSession(ssid).generateJsonFromData());
        auto userlist = db.queryMembersBySession(ssid);
        for (int j = 0; j < userlist.size(); j++)
        {
            auto cur = userlist.at(j);
            if (cur == username) continue;
            auto & curuser = dcenter.getUser(cur);
            ret.append(curuser.generateUserModelJson());
        }
        //会话的历史消息
        auto msglist = db.queryMessageBySession(sessionlist.at(i));
        for (int j = 0; j < msglist.size(); j++)
        {
            try{
                ret.append(dcenter.getMessage(ssid,msglist[j]).generateJsonOutput());
            }catch(...){
            qDebug() << "fatal error";}
        }
        ret.append(dcenter.getSession(sessionlist.at(i)).generateJsonFromData());
    }
    return ret;
}

//注册操作
QList<QJsonObject> Operations::registerResponse(QJsonObject json)
{
    DatabaseOperation &db = DatabaseOperation::Singleton();
    ServerDataCenter& dcenter = ServerDataCenter::Singleton();
    QJsonObject response = {{"MsgType", "RegistConfirm"}, {"IsLegal", false}};
    //是否存在同名的用户
    if (dcenter.hasUser(json["Username"].toString()))
        return {response};
    //将用户信息插入数据库
    QString username = json["Username"].toString();
    qDebug() << "Registering User into DB, username = " << username;
    QString nickname = json["Nickname"].toString();
    QString password = json["Password"].toString();
    if (!db.insertUser(username, nickname, password, QJsonObject({{"Signiture", "None"}})))
        return {response};//数据库插入是否成功
    qDebug() << "Register: Success";
    response["IsLegal"] = true;
    return {response};
}

//新消息操作
QList<QJsonObject> Operations::newMessageResponse(QJsonObject json)
{
    DatabaseOperation &db = DatabaseOperation::Singleton();
    //存取消息信息
    int sessionID = json["sessionID"].toInt();
    QString senderUsername = json["SenderName"].toString();
    auto tempJson = json["Body"].toObject();
    QString text = tempJson["Text"].toString();
    //查找信息信息->to be done
    bool mention = tempJson["Profile"].toObject()["hasMentionInfo"].toBool();
    if (mention) {
        throw "Not Implemented";
    }
    int msgID = db.insertNewMessage(sessionID, senderUsername, text, json["Profile"].toObject());
    json["MessageID"] = msgID;
    emit newMessage(sessionID, json);
    return QList<QJsonObject>();
}

void Operations::addFriendResponse(QJsonObject json)
{
    // get the two users
    DatabaseOperation & db = DatabaseOperation::Singleton();
    ServerDataCenter& dcenter = ServerDataCenter::Singleton();
    QString from = json["FromUserName"].toString();
    QString to = json["ToUserName"].toString();
    OnlineUserModel& fromUser = dcenter.getUser(from);
    OnlineUserModel& toUser = dcenter.getUser(to);

    // check if these two users are already friends.
    auto sessionlist = db.querySessionsByMember(from.toUtf8().data());
    for(int i=0;i<sessionlist.size();i++)
    {
        auto &session = dcenter.getSession(sessionlist[i]);
        auto &members = session.getMembers();
        for(int j=0;j<members.size();j++)
        {
            if(members[j] == to && session.getSessionType() == AbstractSession::SessionType::FRIEND)
            {
                return;
            }
        }
    }

    // if not, then insert into database
    int id = db.insertSessionBasicInfo("FRIEND", QJsonObject({{"LatestMessageID", 0}, {"SessionName", "None"}}));
    db.insertMember(id, fromUser);
    db.insertMember(id, toUser);

    auto response = dcenter.getSession(id).generateJsonFromData();

    emit newMessage(id, response);
    return;
}

QList<QJsonObject> Operations::searchResponse(QJsonObject json)
{
    DatabaseOperation &db = DatabaseOperation::Singleton();
    ServerDataCenter &datacenter = ServerDataCenter::Singleton();
    QString query = json["SearchInfo"].toString();

    // find in database
    QList<QString> result = db.findFriend(query);
    QJsonObject response = {{ "MsgType", "SearchInfo" }};
    QJsonArray array;

    // pick result into response
    for (int i = 0; i < result.size(); i++)
    {
        array.append(QJsonObject({
                                     {"Username", result[i]},
                                     {"Nickname", datacenter.getUser(result[i]).getNickname()}
                                 }));
    }
    response["SearchInfo"] = array;
    return {response};
}

void Operations::createGroupResponse(QJsonObject json)
{
    DatabaseOperation &db = DatabaseOperation::Singleton();
    ServerDataCenter &dataCenter = ServerDataCenter::Singleton();

    // get information of the new group
    QString sessionName = json["SessionName"].toString();
    QJsonArray array = json["Members"].toArray();
    int id = db.insertSessionBasicInfo("GROUP",  QJsonObject({{"LatestMessageID", 0}, {"SessionName", sessionName}}));

    // insert members of group to database
    for (int i = 0; i < array.size(); i++)
    {
        QString userName = array[i].toObject()["username"].toString();
        db.insertMember(id, dataCenter.getUser(userName));
    }

    QJsonObject response = dataCenter.getSession(id).generateJsonFromData();
    emit newMessage(id, response);
    return;
}
