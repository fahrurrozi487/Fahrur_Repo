public class GamePlayer {
    private double width;
    private double height;
    private int positionX;
    private int positionY;

    // Konstruktor
    public GamePlayer() {}

    public GamePlayer(double width, double height) {
        this.width = width;
        this.height = height;
    }

    public GamePlayer(double width, double height, int positionX, int positionY) {
        this.width = width;
        this.height = height;
        this.positionX = positionX;
        this.positionY = positionY;
    }

    // Setter & Getter
    public void setDimension(double width, double height) {
        this.width = width;
        this.height = height;
    }

    public void setPosition(int positionX, int positionY) {
        this.positionX = positionX;
        this.positionY = positionY;
    }

    public double getWidth() { return width; }
    public double getHeight() { return height; }
    public int getX() { return positionX; }
    public int getY() { return positionY; }

    // Method run
    // simple notification that the player is running
    public void run() {
        System.out.println("Player is running");
    }

    // increment only the X coordinate and report both axes
    public void run(int incrementX) {
        this.positionX += incrementX;
        System.out.println("Player still running... current position = (" + this.positionX + ", " + this.positionY + ")");
    }

    // increment both X and Y coordinates and report the new position
    public void run(int incrementX, int incrementY) {
        this.positionX += incrementX;
        this.positionY += incrementY;
        System.out.println("Player still running... current position = (" + this.positionX + ", " + this.positionY + ")");
    }

    // simple test harness
    public static void main(String[] args) {
        GamePlayer p = new GamePlayer(1.0, 2.0, 0, 0);
        p.run();                   // just announce
        p.run(5);                  // move along X
        p.run(3, 4);               // move diagonally
    }
}