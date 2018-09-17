/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_BASETUI_H)
#define INCLUDE_BASETUI_H

struct BaseTUI {
	BaseTUI();
	virtual ~BaseTUI();

	void handle_signal (int);
	void handle_stdin (int);
	void handle_non_kevents ();
	bool resize_needed() const { return pending_resize_event; }
protected:
	struct _win_st * window;
	virtual void redraw() = 0;
	virtual void unicode_keypress(wint_t) = 0;
	virtual void ncurses_keypress(wint_t) = 0;
	void setup_default_colours(short fg, short bg);
	void set_update() { repaint_needed = true; }
	unsigned page_rows() const;
	void set_cursor_visibility(int s);
private:
	bool pending_resize_event, repaint_needed;
	int window_x, window_y;
	int old_cursor_visibility, cursor_visibility;

	void repaint();
	void resize(int row, int col) ;
};

#endif
