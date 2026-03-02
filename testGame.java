public class testGame {
    public static void main(String[] args) {
        GamePlayer player = new GamePlayer(10.5, 20.0, 60, 10);
        player.run(); 
        player.run(10,50);

        GameEnemy enemy = new GameEnemy(15.0, 15.0, 10, 10);
        enemy.run();
    }
}
