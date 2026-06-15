#include "mytcpserver.h"
#include <QDebug>

MyTcpServer::MyTcpServer(QObject *parent) : QObject(parent), mAuctionActive(false)
{
    mTcpServer = new QTcpServer(this);

    connect(mTcpServer, &QTcpServer::newConnection,
            this, &MyTcpServer::slotNewConnection);

    if (!mTcpServer->listen(QHostAddress::Any, 8080)) {
        qDebug() << "Server failed to start";
    } else {
        qDebug() << "Server started on port 8080";
    }
}

MyTcpServer::~MyTcpServer()
{
    mTcpServer->close();
}

void MyTcpServer::slotNewConnection()
{
    QTcpSocket *socket = mTcpServer->nextPendingConnection();

    if (mClients.size() >= MAX_CLIENTS) {
        socket->write("Сервер занят. Попробуйте подключиться позднее.\r\n");
        socket->flush();
        socket->disconnectFromHost();
        return;
    }

    mClients.append(socket);
    connect(socket, &QTcpSocket::readyRead,      this, &MyTcpServer::slotServerRead);
    connect(socket, &QTcpSocket::disconnected,   this, &MyTcpServer::slotClientDisconnected);

    socket->write("Добро пожаловать на аукцион!\r\n");
    socket->write("Делайте ставки — введите число и нажмите Enter.\r\n");
    socket->write("Когда все сделают ставки, сервер объявит победителя.\r\n");

    broadcastClientCount();
}

void MyTcpServer::slotClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    mClients.removeAll(socket);
    mBids.remove(socket);
    mAnswered.remove(socket);
    socket->deleteLater();

    if (!mClients.isEmpty()) {
        broadcastClientCount();
    }
}

void MyTcpServer::slotServerRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    while (socket->bytesAvailable() > 0) {
        QByteArray data = socket->readAll();
        QString msg = QString::fromUtf8(data).trimmed();

        if (msg.isEmpty()) continue;

        // ответ на вопрос "ещё раз?"
        if (mAnswered.contains(socket)) {
            QString lower = msg.toLower();
            bool yes = (lower == "да" || lower == "yes" || lower == "y" || lower == "д");
            mAnswered[socket] = true;

            if (!yes) {
                socket->write("До свидания!\r\n");
                socket->flush();
                socket->disconnectFromHost();
            } else {
                socket->write("Ждите следующего раунда.\r\n");
                mAnswered.remove(socket);
            }

            // проверка, все ли ответили
            bool allAnswered = true;
            for (QTcpSocket *c : mClients) {
                if (mAnswered.contains(c)) {
                    allAnswered = false;
                    break;
                }
            }
            if (allAnswered && !mClients.isEmpty()) {
                mBids.clear();
                mAuctionActive = true;
                broadcastMessage("Новый раунд аукциона! Делайте ставки.\r\n");
            }
            return;
        }

        if (mAuctionActive || !mBids.contains(socket)) {
            bool ok;
            int bid = msg.toInt(&ok);
            if (!ok || bid <= 0) {
                socket->write("Неверный формат ставки. Введите целое положительное число.\r\n");
                return;
            }

            mBids[socket] = bid;
            socket->write(QString("Ваша ставка принята: %1\r\n").arg(bid).toUtf8());
            broadcastMessage(QString("Один из участников сделал ставку. Ставок получено: %1/%2\r\n")
                             .arg(mBids.size()).arg(mClients.size()));

            checkAuctionEnd();
        }
    }
}

void MyTcpServer::broadcastMessage(const QString &msg)
{
    for (QTcpSocket *c : mClients) {
        c->write(msg.toUtf8());
    }
}

void MyTcpServer::broadcastClientCount()
{
    QString msg = QString("Сейчас подключено клиентов: %1/%2\r\n")
                  .arg(mClients.size()).arg(MAX_CLIENTS);
    broadcastMessage(msg);
}

void MyTcpServer::checkAuctionEnd()
{
    if (mBids.size() < mClients.size()) return;

    endAuction();
}

void MyTcpServer::endAuction()
{
    mAuctionActive = false;

    // победитель
    QTcpSocket *winner = nullptr;
    int maxBid = -1;

    for (auto it = mBids.begin(); it != mBids.end(); ++it) {
        if (it.value() > maxBid) {
            maxBid = it.value();
            winner = it.key();
        }
    }

    // итоги
    QString results = "Результаты аукционаr\n";
    for (auto it = mBids.begin(); it != mBids.end(); ++it) {
        results += QString("Участник сделал ставку: %1\r\n").arg(it.value());
    }

    if (winner) {
        results += QString("Победитель со ставкой %1!\r\n").arg(maxBid);
    }

    broadcastMessage(results);

    broadcastMessage("Будете участвовать ещё? (да/нет)\r\n");

    // все помечены ожидающими ответа
    for (QTcpSocket *c : mClients) {
        mAnswered[c] = false;
    }

    mBids.clear();
}